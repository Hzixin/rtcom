#pragma once

#include "common/types.h"
#include "sip/sip_manager.h"
#include "rtp/rtp_session.h"
#include "rtp/rtcp_handler.h"
#include "rtp/jitter_buffer.h"
#include "media/media_encoder.h"
#include "media/media_decoder.h"
#include "net/thread_pool.h"
#include "net/io_multiplexer.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

namespace rtcom {

class SessionManager {
public:
    explicit SessionManager(const ServerConfig& config);
    ~SessionManager();

    bool Start();
    void Stop();

    // Call control
    std::string MakeCall(const std::string& callee_uri);
    bool HangUp(const std::string& call_id);
    bool AcceptCall(const std::string& call_id);

    // Send media (encode + RTP send)
    bool SendAudioFrame(const std::string& call_id, const int16_t* pcm_data, size_t samples);
    bool SendVideoFrame(const std::string& call_id, const uint8_t* yuv_data, size_t len);

    // Call info
    const CallSessionInfo* GetCallInfo(const std::string& call_id) const;
    SipManager& GetSipManager() { return sip_mgr_; }

    // Callbacks
    void SetSipEventCallback(SipEventCallback cb);
    void SetRtpPacketCallback(RtpPacketCallback cb);

private:
    ServerConfig config_;
    SipManager sip_mgr_;
    std::unique_ptr<ThreadPool> thread_pool_;

    struct CallContext {
        std::unique_ptr<RtpSession> audio_rtp;
        std::unique_ptr<RtpSession> video_rtp;
        std::unique_ptr<RtcpHandler> rtcp;
        std::unique_ptr<JitterBuffer> audio_jitter;
        std::unique_ptr<JitterBuffer> video_jitter;
        std::unique_ptr<MediaEncoder> audio_encoder;
        std::unique_ptr<MediaEncoder> video_encoder;
        std::unique_ptr<MediaDecoder> audio_decoder;
        std::unique_ptr<MediaDecoder> video_decoder;
        CallSessionInfo info;
        std::string call_id;
    };

    std::unordered_map<std::string, std::unique_ptr<CallContext>> contexts_;
    mutable std::mutex ctx_mutex_;
    RtpPacketCallback rtp_callback_;

    // RTP receive loop (epoll)
    IoMultiplexer rtp_epoll_;
    std::thread rtp_thread_;
    std::atomic<bool> rtp_running_{false};

    CallContext* GetOrCreateContext(const std::string& call_id, const CallSessionInfo& info);
    void RemoveContext(const std::string& call_id);
    void OnSipEvent(const std::string& call_id, SipCallState state);

    void RegisterRtpFds(CallContext* ctx);
    void UnregisterRtpFds(CallContext* ctx);
    void OnRtpData(const std::string& call_id, RtpPayloadType pt,
                   const uint8_t* payload, size_t len);
    void OnRtcpTimer();
    void RtpEventLoop();

    static uint32_t GenerateSsrc();
    std::chrono::steady_clock::time_point last_rtcp_time_;
};

} // namespace rtcom
