#include <glog/logging.h>
#undef WARNING

#include "session_manager.h"
#include <random>
#include <sys/epoll.h>

namespace rtcom {

static std::mt19937 rng(std::random_device{}());

uint32_t SessionManager::GenerateSsrc() {
    return std::uniform_int_distribution<uint32_t>()(rng);
}

SessionManager::SessionManager(const ServerConfig& config)
    : config_(config), rtp_epoll_(1024) {}

SessionManager::~SessionManager() { Stop(); }

// ============================================================================
// Start / Stop
// ============================================================================

bool SessionManager::Start() {
    if (!sip_mgr_.Initialize(config_.sip)) return false;

    sip_mgr_.SetSipEventCallback(
        [this](const std::string& call_id, SipCallState state) {
            OnSipEvent(call_id, state);
        });

    thread_pool_ = std::make_unique<ThreadPool>(config_.worker_threads);

    // Start RTP receive event loop
    rtp_running_ = true;
    rtp_thread_ = std::thread(&SessionManager::RtpEventLoop, this);

    LOG(INFO) << "SessionManager started, " << config_.worker_threads
              << " workers + RTP receive loop";
    return true;
}

void SessionManager::Stop() {
    rtp_running_ = false;
    rtp_epoll_.Stop();
    if (rtp_thread_.joinable()) rtp_thread_.join();

    thread_pool_.reset();
    sip_mgr_.Shutdown();
    contexts_.clear();
}

// ============================================================================
// Call control
// ============================================================================

std::string SessionManager::MakeCall(const std::string& callee_uri) {
    if (contexts_.size() >= config_.max_sessions) return "";
    return sip_mgr_.MakeCall(callee_uri);
}

bool SessionManager::HangUp(const std::string& call_id) {
    return sip_mgr_.HangUp(call_id);
}

bool SessionManager::AcceptCall(const std::string& call_id) {
    return sip_mgr_.AnswerCall(call_id, true);
}

// ============================================================================
// Send media (encode → RTP send)
// ============================================================================

bool SessionManager::SendAudioFrame(const std::string& call_id,
                                    const int16_t* pcm_data, size_t samples) {
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    auto it = contexts_.find(call_id);
    if (it == contexts_.end() || !it->second->audio_rtp) return false;

    auto& ctx = it->second;
    if (ctx->audio_encoder) {
        std::vector<uint8_t> encoded;
        if (!ctx->audio_encoder->EncodeAudio(pcm_data, samples, encoded))
            return false;
        auto rtp_payload = AacPacketizer::Pack(encoded.data(), encoded.size());
        ctx->audio_rtp->AdvanceTimestamp(samples);
        return ctx->audio_rtp->SendPacket(rtp_payload.data(), rtp_payload.size(),
                                          ctx->info.remote_rtp_addr,
                                          ctx->info.audio_remote_port);
    }

    ctx->audio_rtp->AdvanceTimestamp(samples);
    return ctx->audio_rtp->SendPacket(reinterpret_cast<const uint8_t*>(pcm_data),
                                      samples * sizeof(int16_t),
                                      ctx->info.remote_rtp_addr,
                                      ctx->info.audio_remote_port);
}

bool SessionManager::SendVideoFrame(const std::string& call_id,
                                    const uint8_t* yuv_data, size_t len) {
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    auto it = contexts_.find(call_id);
    if (it == contexts_.end() || !it->second->video_rtp) return false;

    auto& ctx = it->second;
    if (ctx->video_encoder) {
        std::vector<uint8_t> encoded;
        if (!ctx->video_encoder->EncodeVideo(yuv_data, len, encoded))
            return false;
        auto fragments = H264Fragmenter::Fragment(encoded.data(), encoded.size(),
                                                   kRtpMaxPayloadSize);
        for (size_t i = 0; i < fragments.size(); ++i) {
            ctx->video_rtp->SendPacket(fragments[i].data(), fragments[i].size(),
                                       ctx->info.remote_rtp_addr,
                                       ctx->info.video_remote_port,
                                       i == fragments.size() - 1);
        }
        ctx->video_rtp->AdvanceTimestamp(90000 / config_.media.video_fps);
        return true;
    }
    return false;
}

// ============================================================================
// Call info & callbacks
// ============================================================================

const CallSessionInfo* SessionManager::GetCallInfo(const std::string& call_id) const {
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    auto it = contexts_.find(call_id);
    return (it != contexts_.end()) ? &it->second->info : nullptr;
}

void SessionManager::SetSipEventCallback(SipEventCallback cb) {
    sip_mgr_.SetSipEventCallback(std::move(cb));
}

void SessionManager::SetRtpPacketCallback(RtpPacketCallback cb) {
    rtp_callback_ = std::move(cb);
}

// ============================================================================
// Call context lifecycle
// ============================================================================

SessionManager::CallContext* SessionManager::GetOrCreateContext(
    const std::string& call_id, const CallSessionInfo& info) {

    std::lock_guard<std::mutex> lock(ctx_mutex_);
    auto it = contexts_.find(call_id);
    if (it != contexts_.end()) return it->second.get();

    auto ctx = std::make_unique<CallContext>();
    ctx->info = info;
    ctx->call_id = call_id;

    uint32_t audio_ssrc = GenerateSsrc();
    uint32_t video_ssrc = GenerateSsrc();

    // RTP sessions (send + recv)
    ctx->audio_rtp = std::make_unique<RtpSession>(audio_ssrc, RtpPayloadType::kAAC);
    ctx->audio_rtp->OpenSendSocket(config_.media.audio_port);
    ctx->audio_rtp->OpenRecvSocket(config_.media.audio_port + 2);

    ctx->video_rtp = std::make_unique<RtpSession>(video_ssrc, RtpPayloadType::kH264);
    ctx->video_rtp->OpenSendSocket(config_.media.video_port);
    ctx->video_rtp->OpenRecvSocket(config_.media.video_port + 2);

    // RTCP
    ctx->rtcp = std::make_unique<RtcpHandler>(audio_ssrc);

    // Jitter buffers
    JitterBufferConfig jb_cfg;
    jb_cfg.sample_rate = config_.media.audio_sample_rate;
    jb_cfg.initial_delay_ms = 0;  // Immediate output for testing
    ctx->audio_jitter = std::make_unique<JitterBuffer>(jb_cfg);
    ctx->video_jitter = std::make_unique<JitterBuffer>(jb_cfg);

    // Encoders (for sending)
    ctx->audio_encoder = std::make_unique<MediaEncoder>(config_.media.audio_codec);
    ctx->audio_encoder->Initialize(config_.media.audio_bitrate,
                                   config_.media.audio_sample_rate, 1);
    ctx->video_encoder = std::make_unique<MediaEncoder>(config_.media.video_codec);
    ctx->video_encoder->Initialize(config_.media.video_bitrate,
                                   config_.media.audio_sample_rate, 1,
                                   config_.media.video_width,
                                   config_.media.video_height,
                                   config_.media.video_fps);

    // Decoders (for receiving) — NEW
    ctx->audio_decoder = std::make_unique<MediaDecoder>(config_.media.audio_codec);
    ctx->audio_decoder->Initialize();
    ctx->video_decoder = std::make_unique<MediaDecoder>(config_.media.video_codec);
    ctx->video_decoder->Initialize();

    ctx->info.local_ssrc = audio_ssrc;
    ctx->info.audio_local_port = ctx->audio_rtp->LocalSendPort();
    ctx->info.video_local_port = ctx->video_rtp->LocalSendPort();

    auto* ptr = ctx.get();
    contexts_[call_id] = std::move(ctx);

    // Register RTP receive fds with epoll
    RegisterRtpFds(ptr);

    LOG(INFO) << "Context created for " << call_id
              << " audio_port=" << ptr->info.audio_local_port
              << " video_port=" << ptr->info.video_local_port;
    return ptr;
}

void SessionManager::RemoveContext(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    auto it = contexts_.find(call_id);
    if (it != contexts_.end()) {
        UnregisterRtpFds(it->second.get());
        contexts_.erase(it);
    }
}

void SessionManager::OnSipEvent(const std::string& call_id, SipCallState state) {
    LOG(INFO) << "SIP event: " << call_id << " -> " << SipCallStateToString(state);

    switch (state) {
        case SipCallState::kConnected: {
            auto* call = sip_mgr_.GetCall(call_id);
            if (call) {
                auto* ctx = GetOrCreateContext(call_id, call->SessionInfo());
                // Sync context ports back to SIP call (for SDP answer)
                if (ctx) {
                    call->SessionInfo().audio_local_port = ctx->info.audio_local_port;
                    call->SessionInfo().video_local_port = ctx->info.video_local_port;
                }
            }
            break;
        }
        case SipCallState::kDisconnected:
        case SipCallState::kFailed:
            RemoveContext(call_id);
            break;
        default:
            break;
    }
}

// ============================================================================
// RTP Receive: register/unregister fds with epoll
// ============================================================================

void SessionManager::RegisterRtpFds(CallContext* ctx) {
    std::string cid = ctx->call_id;

    // Audio recv fd
    int audio_fd = ctx->audio_rtp->RecvFd();
    if (audio_fd >= 0) {
        rtp_epoll_.AddFd(audio_fd, EPOLLIN, [this, cid](int, uint32_t) {
            std::lock_guard<std::mutex> lock(ctx_mutex_);
            auto it = contexts_.find(cid);
            if (it == contexts_.end()) return;
            auto& c = it->second;

            // Receive raw RTP packet
            int n = c->audio_rtp->ReceivePacket();
            if (n <= 0) return;

            // Insert into jitter buffer
            const auto& last_pkt = c->audio_rtp->GetLastPacket();
            JitterBuffer* jb = c->audio_jitter.get();
            if (jb) {
                RtpPacket copy = last_pkt;  // Copy since Insert takes rvalue
                jb->Insert(std::move(copy));

                // Extract ready packets and decode
                RtpPacket* ready = jb->Extract();
                if (ready) {
                    OnRtpData(cid, static_cast<RtpPayloadType>(ready->PayloadType()),
                              ready->PayloadData(), ready->PayloadSize());
                }
            }
        });
        LOG(INFO) << "Registered audio recv fd=" << audio_fd << " for " << cid;
    }

    // Video recv fd
    int video_fd = ctx->video_rtp->RecvFd();
    if (video_fd >= 0) {
        rtp_epoll_.AddFd(video_fd, EPOLLIN, [this, cid](int, uint32_t) {
            std::lock_guard<std::mutex> lock(ctx_mutex_);
            auto it = contexts_.find(cid);
            if (it == contexts_.end()) return;
            auto& c = it->second;

            int n = c->video_rtp->ReceivePacket();
            if (n <= 0) return;

            const auto& last_pkt = c->video_rtp->GetLastPacket();
            JitterBuffer* jb = c->video_jitter.get();
            if (jb) {
                RtpPacket copy = last_pkt;
                jb->Insert(std::move(copy));

                RtpPacket* ready = jb->Extract();
                if (ready) {
                    OnRtpData(cid, RtpPayloadType::kH264,
                              ready->PayloadData(), ready->PayloadSize());
                }
            }
        });
        LOG(INFO) << "Registered video recv fd=" << video_fd << " for " << cid;
    }
}

void SessionManager::UnregisterRtpFds(CallContext* ctx) {
    int audio_fd = ctx->audio_rtp->RecvFd();
    if (audio_fd >= 0) rtp_epoll_.DelFd(audio_fd);

    int video_fd = ctx->video_rtp->RecvFd();
    if (video_fd >= 0) rtp_epoll_.DelFd(video_fd);

    LOG(INFO) << "Unregistered RTP fds for " << ctx->call_id;
}

// ============================================================================
// Process decoded RTP data
// ============================================================================

void SessionManager::OnRtpData(const std::string& call_id, RtpPayloadType pt,
                               const uint8_t* payload, size_t len) {
    if (!payload || len == 0) return;

    auto it = contexts_.find(call_id);
    if (it == contexts_.end()) return;
    auto& ctx = it->second;

    // Decode and invoke callback
    if (pt == RtpPayloadType::kAAC && ctx->audio_decoder) {
        // Unpack AAC from RTP
        std::vector<uint8_t> aac_data;
        if (AacPacketizer::Unpack(payload, len, aac_data)) {
            std::vector<int16_t> pcm;
            if (ctx->audio_decoder->DecodeAudio(aac_data.data(), aac_data.size(), pcm)) {
                LOG(INFO) << "Decoded audio: " << pcm.size() << " samples for " << call_id;
                if (rtp_callback_) {
                    rtp_callback_(call_id, reinterpret_cast<const uint8_t*>(pcm.data()),
                                  pcm.size() * sizeof(int16_t), pt);
                }
            }
        } else {
            LOG(WARNING) << "AAC unpack failed for " << call_id;
        }
    } else if (pt == RtpPayloadType::kPCMU || pt == RtpPayloadType::kPCMA) {
        // Raw G.711 audio — pass through directly
        LOG(INFO) << "Raw audio: " << len << " bytes (PT=" << static_cast<int>(pt)
                  << ") for " << call_id;
        if (rtp_callback_) {
            rtp_callback_(call_id, payload, len, pt);
        }
    } else if (pt == RtpPayloadType::kH264 && ctx->video_decoder) {
        std::vector<uint8_t> yuv;
        int w = 0, h = 0;
        if (ctx->video_decoder->Decode(payload, len, yuv, w, h)) {
            LOG(INFO) << "Decoded video: " << w << "x" << h << " " << yuv.size()
                      << " bytes for " << call_id;
            if (rtp_callback_) {
                rtp_callback_(call_id, yuv.data(), yuv.size(), pt);
            }
        }
    } else {
        LOG(WARNING) << "Unsupported payload type: " << static_cast<int>(pt)
                     << " for " << call_id;
    }
}

// ============================================================================
// RTP Event Loop (epoll + RTCP timer)
// ============================================================================

void SessionManager::OnRtcpTimer() {
    // Send RTCP compound packet for each active call
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    for (auto& pair : contexts_) {
        auto& ctx = pair.second;
        if (!ctx->rtcp || !ctx->audio_rtp) continue;

        auto compound = ctx->rtcp->BuildCompound(
            ctx->info.callee_uri.empty() ? "rtcom@unknown" : ctx->info.callee_uri,
            RtcpHandler::NowNtp(),
            ctx->audio_rtp->CurrentTimestamp(),
            ctx->audio_rtp->PacketsSent(),
            ctx->audio_rtp->BytesSent());

        // Send RTCP to the remote RTP port (+2 from audio port)
        if (!ctx->info.remote_rtp_addr.empty()) {
            int rtcp_fd = ctx->audio_rtp->SendFd();
            if (rtcp_fd >= 0) {
                struct sockaddr_in dest;
                memset(&dest, 0, sizeof(dest));
                dest.sin_family = AF_INET;
                dest.sin_port = htons(ctx->info.audio_remote_port + 1);  // RTCP = audio+1
                inet_pton(AF_INET, ctx->info.remote_rtp_addr.c_str(), &dest.sin_addr);
                sendto(rtcp_fd, compound.data(), compound.size(), 0,
                       (struct sockaddr*)&dest, sizeof(dest));
            }
        }
    }
}

void SessionManager::RtpEventLoop() {
    LOG(INFO) << "RTP receive loop started";

    // RTCP periodic timer (every 5 seconds)
    rtp_epoll_.AddTimer(5000, true, [this]() { OnRtcpTimer(); });

    // Blocking epoll loop (runs until Stop() is called)
    rtp_epoll_.Run(1000);  // 1s timeout for checking rtp_running_

    LOG(INFO) << "RTP receive loop stopped";
}

} // namespace rtcom
