#include <glog/logging.h>
#undef WARNING

#include "sip_call.h"
#include <glog/logging.h>

namespace rtcom {

SipCall::SipCall(const std::string& call_id, const std::string& local_uri,
                 const std::string& remote_uri)
    : call_id_(call_id), local_uri_(local_uri), remote_uri_(remote_uri) {
    LOG(INFO) << "SIP call created: " << call_id_;
}

SipCall::~SipCall() {
    LOG(INFO) << "SIP call destroyed: " << call_id_;
}

void SipCall::SetState(SipCallState state) {
    if (state_ != state) {
        LOG(INFO) << "Call " << call_id_ << ": "
                  << SipCallStateToString(state_) << " -> " << SipCallStateToString(state);
        state_ = state;
        session_info_.state = state;
    }
}

void SipCall::StartTimer(uint32_t timeout_ms) {
    timer_start_ = std::chrono::steady_clock::now();
    timer_duration_ms_ = timeout_ms;
    retransmit_count_ = 0;
}

bool SipCall::IsTimerExpired() const {
    if (timer_duration_ms_ == 0) return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - timer_start_).count();
    return static_cast<uint32_t>(elapsed) >= timer_duration_ms_;
}

} // namespace rtcom
