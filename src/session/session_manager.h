#pragma once

#include "common/types.h"
#include "sip/sip_manager.h"
#include "rtp/rtp_session.h"
#include "rtp/rtcp_handler.h"
#include "rtp/jitter_buffer.h"
#include "media/media_encoder.h"
#include "net/thread_pool.h"
#include <unordered_map>
#include <memory>
#include <mutex>

namespace rtcom {

class SessionManager {
public:
    explicit SessionManager(const ServerConfig& config);
    ~SessionManager();

    bool Start();
    void Stop();

    std::string MakeCall(const std::string& callee_uri);
    bool HangUp(const std::string& call_id);
    bool AcceptCall(const std::string& call_id);

    bool SendAudioFrame(const std::string& call_id, const int16_t* pcm_data, size_t samples);
    bool SendVideoFrame(const std::string& call_id, const uint8_t* yuv_data, size_t len);

    const CallSessionInfo* GetCallInfo(const std::string& call_id) const;
    SipManager& GetSipManager() { return sip_mgr_; }

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
        CallSessionInfo info;
    };

    std::unordered_map<std::string, std::unique_ptr<CallContext>> contexts_;
    mutable std::mutex ctx_mutex_;
    RtpPacketCallback rtp_callback_;

    CallContext* GetOrCreateContext(const std::string& call_id, const CallSessionInfo& info);
    void RemoveContext(const std::string& call_id);
    void OnSipEvent(const std::string& call_id, SipCallState state);
    static uint32_t GenerateSsrc();
};

} // namespace rtcom
