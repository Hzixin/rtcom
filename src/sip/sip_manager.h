#pragma once

#include "common/types.h"
#include "sip_call.h"
#include <osip2/osip.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>

namespace rtcom {

class SipManager {
public:
    SipManager();
    ~SipManager();

    bool Initialize(const SipConfig& config);
    void Shutdown();

    bool Register();
    bool Unregister();

    std::string MakeCall(const std::string& callee_uri);
    bool AnswerCall(const std::string& call_id, bool accept = true);
    bool HangUp(const std::string& call_id);
    bool CancelCall(const std::string& call_id);

    void ProcessIncomingMessage(const std::string& raw_msg,
                                const std::string& src_addr, uint16_t src_port);
    void ProcessTimers();

    bool IsRegistered() const { return registered_; }
    SipCallState GetCallState(const std::string& call_id) const;
    SipCall* GetCall(const std::string& call_id);

    std::string BuildRegisterRequest();     // non-const: modifies cseq
    std::string BuildInviteRequest(const std::string& call_id, const std::string& callee_uri);
    std::string BuildAckRequest(const std::string& call_id);
    std::string BuildByeRequest(const std::string& call_id);
    std::string BuildResponse(int status_code, const std::string& reason_phrase,
                              const std::string& to_tag = "");
    std::string BuildResponseWithSdp(int status_code, const std::string& reason_phrase,
                                     const std::string& sdp, const std::string& to_tag = "");

    // Build response that correctly echoes INVITE headers (Via/From/To/Call-ID/CSeq)
    std::string BuildInviteResponse(int status_code, const std::string& reason,
                                    const std::string& sdp = "");

    // SDP negotiation: parse incoming SDP, extract remote RTP port/IP
    bool ProcessIncomingSdp(const std::string& call_id, const std::string& sdp_body);
    std::string BuildSdpAnswer(const std::string& call_id);

    // Store last parsed message headers for response echoing
    void CacheLastRequest(const std::string& call_id);

    void SetSipEventCallback(SipEventCallback cb) { sip_event_cb_ = std::move(cb); }
    const SipConfig& GetConfig() const { return config_; }
    const std::unordered_map<std::string, std::unique_ptr<SipCall>>& Calls() const { return calls_; }

private:
    SipConfig config_;
    osip_t* osip_{nullptr};
    bool initialized_{false};
    bool registered_{false};

    std::unordered_map<std::string, std::unique_ptr<SipCall>> calls_;
    SipEventCallback sip_event_cb_;

    uint32_t local_cseq_{1};
    std::string branch_id_;
    std::string call_id_prefix_;

    // Cached from last INVITE — echoed in response
    std::string last_via_branch_;
    std::string last_from_tag_;
    std::string last_to_tag_;    // set from To header
    std::string last_call_id_;
    std::string last_cseq_;

    std::string GenerateBranchId();
    std::string GenerateCallId();
    std::string GenerateTag();
    uint32_t GenerateCSeq();

    void HandleInvite(osip_message_t* msg, const std::string& src_addr);
    void HandleAck(osip_message_t* msg);
    void HandleBye(osip_message_t* msg);
    void HandleResponse(osip_message_t* msg);
    void HandleCancel(osip_message_t* msg);
    void NotifyStateChange(const std::string& call_id, SipCallState state);
};

} // namespace rtcom
