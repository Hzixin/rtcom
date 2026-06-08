#pragma once

#include "common/types.h"
#include <osip2/osip_mt.h>
#include <osip2/osip_dialog.h>
#include <string>
#include <chrono>
#include <memory>

namespace rtcom {

class SipCall {
public:
    SipCall(const std::string& call_id, const std::string& local_uri,
            const std::string& remote_uri);
    ~SipCall();

    SipCallState GetState() const { return state_; }
    void SetState(SipCallState state);

    const std::string& CallId() const { return call_id_; }
    const std::string& LocalUri() const { return local_uri_; }
    const std::string& RemoteUri() const { return remote_uri_; }

    void SetInviteTransaction(osip_transaction_t* tr) { invite_tr_ = tr; }
    void SetByeTransaction(osip_transaction_t* tr) { bye_tr_ = tr; }
    osip_transaction_t* GetInviteTransaction() const { return invite_tr_; }
    osip_transaction_t* GetByeTransaction() const { return bye_tr_; }
    void SetDialog(osip_dialog_t* dlg) { dialog_ = dlg; }
    osip_dialog_t* GetDialog() const { return dialog_; }

    void StartTimer(uint32_t timeout_ms);
    bool IsTimerExpired() const;
    uint32_t RetransmitCount() const { return retransmit_count_; }
    void IncrementRetransmit() { retransmit_count_++; }
    void ResetRetransmit() { retransmit_count_ = 0; }

    CallSessionInfo& SessionInfo() { return session_info_; }
    const CallSessionInfo& SessionInfo() const { return session_info_; }

    void SetError(const std::string& error) { last_error_ = error; }
    const std::string& LastError() const { return last_error_; }

private:
    std::string call_id_;
    std::string local_uri_;
    std::string remote_uri_;
    SipCallState state_{SipCallState::kIdle};

    osip_transaction_t* invite_tr_{nullptr};
    osip_transaction_t* bye_tr_{nullptr};
    osip_dialog_t* dialog_{nullptr};

    std::chrono::steady_clock::time_point timer_start_;
    uint32_t timer_duration_ms_{0};
    uint32_t retransmit_count_{0};

    CallSessionInfo session_info_;
    std::string last_error_;
};

} // namespace rtcom
