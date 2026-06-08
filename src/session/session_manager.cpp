#include <glog/logging.h>
#undef WARNING

#include "session_manager.h"
#include <glog/logging.h>
#include <random>

namespace rtcom {

static std::mt19937 rng(std::random_device{}());

uint32_t SessionManager::GenerateSsrc() {
    return std::uniform_int_distribution<uint32_t>()(rng);
}

SessionManager::SessionManager(const ServerConfig& config) : config_(config) {}

SessionManager::~SessionManager() { Stop(); }

bool SessionManager::Start() {
    if (!sip_mgr_.Initialize(config_.sip)) return false;

    sip_mgr_.SetSipEventCallback(
        [this](const std::string& call_id, SipCallState state) {
            OnSipEvent(call_id, state);
        });

    thread_pool_ = std::make_unique<ThreadPool>(config_.worker_threads);
    LOG(INFO) << "SessionManager started, " << config_.worker_threads << " workers";
    return true;
}

void SessionManager::Stop() {
    thread_pool_.reset();
    sip_mgr_.Shutdown();
    contexts_.clear();
}

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

SessionManager::CallContext* SessionManager::GetOrCreateContext(
    const std::string& call_id, const CallSessionInfo& info) {

    std::lock_guard<std::mutex> lock(ctx_mutex_);
    auto it = contexts_.find(call_id);
    if (it != contexts_.end()) return it->second.get();

    auto ctx = std::make_unique<CallContext>();
    ctx->info = info;

    uint32_t audio_ssrc = GenerateSsrc();
    uint32_t video_ssrc = GenerateSsrc();

    ctx->audio_rtp = std::make_unique<RtpSession>(audio_ssrc, RtpPayloadType::kAAC);
    ctx->audio_rtp->OpenSendSocket(config_.media.audio_port);
    ctx->audio_rtp->OpenRecvSocket(config_.media.audio_port + 2);

    ctx->video_rtp = std::make_unique<RtpSession>(video_ssrc, RtpPayloadType::kH264);
    ctx->video_rtp->OpenSendSocket(config_.media.video_port);
    ctx->video_rtp->OpenRecvSocket(config_.media.video_port + 2);

    ctx->rtcp = std::make_unique<RtcpHandler>(audio_ssrc);

    JitterBufferConfig jb_cfg;
    jb_cfg.sample_rate = config_.media.audio_sample_rate;
    ctx->audio_jitter = std::make_unique<JitterBuffer>(jb_cfg);
    ctx->video_jitter = std::make_unique<JitterBuffer>(jb_cfg);

    ctx->audio_encoder = std::make_unique<MediaEncoder>(config_.media.audio_codec);
    ctx->audio_encoder->Initialize(config_.media.audio_bitrate,
                                   config_.media.audio_sample_rate, 1);
    ctx->video_encoder = std::make_unique<MediaEncoder>(config_.media.video_codec);
    ctx->video_encoder->Initialize(config_.media.video_bitrate,
                                   config_.media.audio_sample_rate, 1,
                                   config_.media.video_width,
                                   config_.media.video_height,
                                   config_.media.video_fps);

    ctx->info.local_ssrc = audio_ssrc;
    ctx->info.audio_local_port = ctx->audio_rtp->LocalSendPort();
    ctx->info.video_local_port = ctx->video_rtp->LocalSendPort();

    auto* ptr = ctx.get();
    contexts_[call_id] = std::move(ctx);
    LOG(INFO) << "Context created for " << call_id;
    return ptr;
}

void SessionManager::RemoveContext(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    contexts_.erase(call_id);
}

void SessionManager::OnSipEvent(const std::string& call_id, SipCallState state) {
    LOG(INFO) << "SIP event: " << call_id << " -> " << SipCallStateToString(state);
    switch (state) {
        case SipCallState::kConnected: {
            auto* call = sip_mgr_.GetCall(call_id);
            if (call) GetOrCreateContext(call_id, call->SessionInfo());
            break;
        }
        case SipCallState::kDisconnected:
        case SipCallState::kFailed:
            RemoveContext(call_id);
            break;
        default: break;
    }
}

} // namespace rtcom
