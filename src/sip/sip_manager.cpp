#include <glog/logging.h>
#undef WARNING

#include "sip_manager.h"
#include "common/sdp.h"
#include <osip2/osip_mt.h>
#include <osipparser2/osip_parser.h>
#include <sstream>
#include <random>
#include <cstdio>

namespace rtcom {

static std::mt19937 rng(std::random_device{}());
static std::string RandomHex(size_t len) {
    static const char hex[] = "0123456789abcdef";
    std::string r(len, '\0');
    std::uniform_int_distribution<int> dist(0, 15);
    for (size_t i = 0; i < len; ++i) r[i] = hex[dist(rng)];
    return r;
}

SipManager::SipManager() {}
SipManager::~SipManager() { Shutdown(); }

bool SipManager::Initialize(const SipConfig& config) {
    config_ = config;
    if (osip_init(&osip_) != 0) {
        LOG(ERROR) << "osip_init failed";
        return false;
    }
    branch_id_ = "z9hG4bK" + RandomHex(32);
    call_id_prefix_ = RandomHex(16);
    local_cseq_ = std::uniform_int_distribution<uint32_t>(1, 10000)(rng);
    initialized_ = true;
    LOG(INFO) << "SIP Manager initialized: " << config_.username << "@" << config_.domain;
    return true;
}

void SipManager::Shutdown() {
    if (registered_) Unregister();
    calls_.clear();
    if (osip_) { osip_release(osip_); osip_ = nullptr; }
    initialized_ = false;
}

std::string SipManager::GenerateBranchId() { return "z9hG4bK" + RandomHex(32); }
std::string SipManager::GenerateCallId()  { return call_id_prefix_ + "-" + RandomHex(16); }
std::string SipManager::GenerateTag()     { return RandomHex(16); }
uint32_t SipManager::GenerateCSeq()       { return ++local_cseq_; }

bool SipManager::Register() {
    if (!initialized_) return false;
    LOG(INFO) << "Sending REGISTER";
    registered_ = true;
    if (sip_event_cb_) sip_event_cb_("", SipCallState::kRegistered);
    return true;
}

bool SipManager::Unregister() {
    if (!registered_) return false;
    LOG(INFO) << "Sending UNREGISTER";
    registered_ = false;
    if (sip_event_cb_) sip_event_cb_("", SipCallState::kUnregistered);
    return true;
}

std::string SipManager::MakeCall(const std::string& callee_uri) {
    if (!initialized_) return "";
    std::string call_id = GenerateCallId();
    std::string local_uri = config_.username + "@" + config_.domain;

    auto call = std::make_unique<SipCall>(call_id, local_uri, callee_uri);
    call->SetState(SipCallState::kCalling);
    call->StartTimer(config_.retry_t1_ms);

    BuildInviteRequest(call_id, callee_uri);
    LOG(INFO) << "Sending INVITE: " << call_id;

    calls_[call_id] = std::move(call);
    NotifyStateChange(call_id, SipCallState::kCalling);
    return call_id;
}

bool SipManager::AnswerCall(const std::string& call_id, bool accept) {
    auto it = calls_.find(call_id);
    if (it == calls_.end()) return false;
    auto& call = it->second;
    int code = accept ? 200 : 486;
    BuildResponse(code, accept ? "OK" : "Busy Here", GenerateTag());
    LOG(INFO) << "Sending " << code << " for call " << call_id;

    if (accept) {
        call->SetState(SipCallState::kConnected);
        call->SessionInfo().start_time = std::chrono::steady_clock::now();
        NotifyStateChange(call_id, SipCallState::kConnected);
    } else {
        call->SetState(SipCallState::kDisconnected);
        NotifyStateChange(call_id, SipCallState::kDisconnected);
        calls_.erase(it);
    }
    return accept;
}

bool SipManager::HangUp(const std::string& call_id) {
    auto it = calls_.find(call_id);
    if (it == calls_.end()) return false;
    auto& call = it->second;
    if (call->GetState() != SipCallState::kConnected &&
        call->GetState() != SipCallState::kInProgress) return false;
    call->SetState(SipCallState::kDisconnecting);
    BuildByeRequest(call_id);
    LOG(INFO) << "Sending BYE for call " << call_id;
    NotifyStateChange(call_id, SipCallState::kDisconnecting);
    return true;
}

bool SipManager::CancelCall(const std::string& call_id) {
    auto it = calls_.find(call_id);
    if (it == calls_.end()) return false;
    it->second->SetState(SipCallState::kDisconnected);
    NotifyStateChange(call_id, SipCallState::kDisconnected);
    calls_.erase(it);
    return true;
}

// ========== SIP Message Builders (non-const: modify cseq, generate tags) ==========

std::string SipManager::BuildRegisterRequest() {
    std::ostringstream m;
    std::string lu = "sip:" + config_.username + "@" + config_.domain;
    m << "REGISTER sip:" << config_.domain << " SIP/2.0\r\n";
    m << "Via: SIP/2.0/UDP " << config_.local_addr << ":" << config_.local_port
      << ";branch=" << branch_id_ << "\r\n";
    m << "Max-Forwards: 70\r\n";
    m << "From: <" << lu << ">;tag=" << RandomHex(16) << "\r\n";
    m << "To: <" << lu << ">\r\n";
    m << "Call-ID: " << RandomHex(16) << "\r\n";
    m << "CSeq: " << (local_cseq_ + 1) << " REGISTER\r\n";
    m << "Contact: <" << lu << ">\r\n";
    m << "Expires: " << config_.register_expires << "\r\n";
    m << "Content-Length: 0\r\n\r\n";
    return m.str();
}

std::string SipManager::BuildInviteRequest(const std::string& call_id,
                                           const std::string& callee_uri) {
    std::ostringstream m;
    std::string lu = "sip:" + config_.username + "@" + config_.domain;
    m << "INVITE " << callee_uri << " SIP/2.0\r\n";
    m << "Via: SIP/2.0/UDP " << config_.local_addr << ":" << config_.local_port
      << ";branch=" << GenerateBranchId() << "\r\n";
    m << "Max-Forwards: 70\r\n";
    m << "From: <" << lu << ">;tag=" << GenerateTag() << "\r\n";
    m << "To: <" << callee_uri << ">\r\n";
    m << "Call-ID: " << call_id << "\r\n";
    m << "CSeq: " << GenerateCSeq() << " INVITE\r\n";
    m << "Contact: <" << lu << ">\r\n";
    m << "Content-Type: application/sdp\r\n";
    // Build and attach SDP
    std::string sdp = SdpBuilder::BuildOffer(config_.local_addr,
        config_.local_port + 1000, config_.local_port + 1002, 96, 97);
    m << "Content-Length: " << sdp.size() << "\r\n\r\n";
    m << sdp;    return m.str();
}

std::string SipManager::BuildAckRequest(const std::string& call_id) {
    auto it = calls_.find(call_id);
    if (it == calls_.end()) return "";
    auto& call = it->second;
    std::ostringstream m;
    m << "ACK " << call->RemoteUri() << " SIP/2.0\r\n";
    m << "Via: SIP/2.0/UDP " << config_.local_addr << ":" << config_.local_port
      << ";branch=" << GenerateBranchId() << "\r\n";
    m << "From: <" << call->LocalUri() << ">;tag=" << GenerateTag() << "\r\n";
    m << "To: <" << call->RemoteUri() << ">;tag=" << GenerateTag() << "\r\n";
    m << "Call-ID: " << call_id << "\r\n";
    m << "CSeq: " << GenerateCSeq() << " ACK\r\n";
    m << "Content-Length: 0\r\n\r\n";
    return m.str();
}

std::string SipManager::BuildByeRequest(const std::string& call_id) {
    auto it = calls_.find(call_id);
    if (it == calls_.end()) return "";
    auto& call = it->second;
    std::ostringstream m;
    m << "BYE " << call->RemoteUri() << " SIP/2.0\r\n";
    m << "Via: SIP/2.0/UDP " << config_.local_addr << ":" << config_.local_port
      << ";branch=" << GenerateBranchId() << "\r\n";
    m << "From: <" << call->LocalUri() << ">;tag=" << GenerateTag() << "\r\n";
    m << "To: <" << call->RemoteUri() << ">;tag=" << GenerateTag() << "\r\n";
    m << "Call-ID: " << call_id << "\r\n";
    m << "CSeq: " << GenerateCSeq() << " BYE\r\n";
    m << "Content-Length: 0\r\n\r\n";
    return m.str();
}

std::string SipManager::BuildResponse(int status_code, const std::string& reason,
                                      const std::string& to_tag) {
    std::ostringstream m;
    std::string lu = "sip:" + config_.username + "@" + config_.domain;
    m << "SIP/2.0 " << status_code << " " << reason << "\r\n";
    m << "Via: SIP/2.0/UDP " << config_.local_addr << ":" << config_.local_port
      << ";branch=" << GenerateBranchId() << "\r\n";
    m << "From: <sip:remote@" << config_.domain << ">;tag=" << GenerateTag() << "\r\n";
    m << "To: <" << lu << ">;tag=" << (to_tag.empty() ? GenerateTag() : to_tag) << "\r\n";
    m << "Call-ID: " << RandomHex(16) << "\r\n";
    m << "CSeq: " << GenerateCSeq() << " INVITE\r\n";
    m << "Contact: <" << lu << ">\r\n";
    m << "Content-Length: 0\r\n\r\n";
    return m.str();
}

// ========== Incoming Message Processing (libosip2 v4 API) ==========

void SipManager::ProcessIncomingMessage(const std::string& raw_msg,
                                        const std::string& src_addr, uint16_t) {
    osip_message_t* msg = nullptr;
    if (osip_message_init(&msg) != 0) return;
    if (osip_message_parse(msg, raw_msg.c_str(), raw_msg.size()) != 0) {
        osip_message_free(msg);
        LOG(WARNING) << "Failed to parse SIP message";
        return;
    }

    if (MSG_IS_REQUEST(msg)) {
        // Get method string from the message
        const char* method = osip_message_get_method(msg);
        if (!method) { osip_message_free(msg); return; }

        std::string smethod(method);
        if (smethod == "INVITE") HandleInvite(msg, src_addr);
        else if (smethod == "ACK") HandleAck(msg);
        else if (smethod == "BYE") HandleBye(msg);
        else if (smethod == "CANCEL") HandleCancel(msg);
    } else if (MSG_IS_RESPONSE(msg)) {
        HandleResponse(msg);
    }
    osip_message_free(msg);
}

void SipManager::HandleInvite(osip_message_t* msg, const std::string&) {
    // libosip2 v4: osip_message_get_call_id returns osip_call_id_t*
    osip_call_id_t* call_id_obj = osip_message_get_call_id(msg);
    std::string call_id = call_id_obj ? (call_id_obj->number ? call_id_obj->number : "") : "";
    if (call_id.empty()) return;

    // Cache headers for response echoing
    last_call_id_ = call_id;

    // Extract Via branch
    if (osip_list_size(&msg->vias) > 0) {
        osip_via_t* via = (osip_via_t*)osip_list_get(&msg->vias, 0);
        osip_generic_param_t* branch_param = nullptr;
        osip_via_param_get_byname(via, "branch", &branch_param);
        if (branch_param && branch_param->gvalue) last_via_branch_ = branch_param->gvalue;
    }

    // Extract From tag
    osip_generic_param_t* tag_param = nullptr;
    osip_from_get_tag(msg->from, &tag_param);
    if (tag_param && tag_param->gvalue) last_from_tag_ = tag_param->gvalue;

    // Extract CSeq (number is char*, method is char*)
    if (msg->cseq) {
        last_cseq_ = std::string(msg->cseq->number ? msg->cseq->number : "0")
                     + " " + (msg->cseq->method ? msg->cseq->method : "INVITE");
    }

    // Get remote URI from From header
    std::string remote = "unknown";
    if (msg->from && msg->from->url) {
        remote = std::string(msg->from->url->username ? msg->from->url->username : "") +
                 "@" + std::string(msg->from->url->host ? msg->from->url->host : "");
    }
    std::string local_uri = config_.username + "@" + config_.domain;

    if (calls_.find(call_id) == calls_.end()) {
        auto call = std::make_unique<SipCall>(call_id, local_uri, "sip:" + remote);
        call->SetState(SipCallState::kRinging);
        call->StartTimer(config_.retry_t1_ms);
        calls_[call_id] = std::move(call);
        NotifyStateChange(call_id, SipCallState::kRinging);

        // Auto-answer with 200 OK
        BuildResponse(200, "OK", GenerateTag());
        if (calls_.find(call_id) != calls_.end()) {
            calls_[call_id]->SetState(SipCallState::kConnected);
            calls_[call_id]->SessionInfo().start_time = std::chrono::steady_clock::now();
            NotifyStateChange(call_id, SipCallState::kConnected);
        }
    }
}

void SipManager::HandleAck(osip_message_t*) {
    LOG(INFO) << "Received ACK";
}

void SipManager::HandleBye(osip_message_t* msg) {
    osip_call_id_t* call_id_obj = osip_message_get_call_id(msg);
    std::string call_id = call_id_obj ? (call_id_obj->number ? call_id_obj->number : "") : "";
    LOG(INFO) << "Received BYE for " << call_id;

    auto it = calls_.find(call_id);
    if (it != calls_.end()) {
        it->second->SetState(SipCallState::kDisconnected);
        NotifyStateChange(call_id, SipCallState::kDisconnected);
        calls_.erase(it);
    }
    BuildResponse(200, "OK");
}

void SipManager::HandleResponse(osip_message_t* msg) {
    int status = msg->status_code;
    osip_call_id_t* call_id_obj = osip_message_get_call_id(msg);
    std::string call_id = call_id_obj ? (call_id_obj->number ? call_id_obj->number : "") : "";
    LOG(INFO) << "Response " << status << " for " << call_id;

    auto it = calls_.find(call_id);
    if (it == calls_.end()) return;
    auto& call = it->second;

    if (status >= 200 && status < 300) {
        if (call->GetState() == SipCallState::kCalling) {
            BuildAckRequest(call_id);
            call->SetState(SipCallState::kConnected);
            NotifyStateChange(call_id, SipCallState::kConnected);
        } else if (call->GetState() == SipCallState::kDisconnecting) {
            call->SetState(SipCallState::kDisconnected);
            NotifyStateChange(call_id, SipCallState::kDisconnected);
            calls_.erase(it);
        }
    } else if (status >= 300) {
        call->SetState(SipCallState::kFailed);
        call->SetError("SIP " + std::to_string(status));
        NotifyStateChange(call_id, SipCallState::kFailed);
    }
}

void SipManager::HandleCancel(osip_message_t* msg) {
    osip_call_id_t* call_id_obj = osip_message_get_call_id(msg);
    std::string call_id = call_id_obj ? (call_id_obj->number ? call_id_obj->number : "") : "";
    LOG(INFO) << "Received CANCEL for " << call_id;

    auto it = calls_.find(call_id);
    if (it != calls_.end()) {
        it->second->SetState(SipCallState::kDisconnected);
        NotifyStateChange(call_id, SipCallState::kDisconnected);
        calls_.erase(it);
    }
}

void SipManager::ProcessTimers() {
    for (auto it = calls_.begin(); it != calls_.end();) {
        auto& call = it->second;
        if (call->IsTimerExpired() && call->RetransmitCount() < config_.max_retransmit) {
            call->IncrementRetransmit();
            LOG(WARNING) << "Timer expired, retransmit #" << call->RetransmitCount()
                         << " for " << call->CallId();
            call->StartTimer(config_.retry_t1_ms * (1 << call->RetransmitCount()));
            ++it;
        } else if (call->IsTimerExpired()) {
            LOG(ERROR) << "Max retransmit for " << call->CallId();
            call->SetState(SipCallState::kFailed);
            NotifyStateChange(call->CallId(), SipCallState::kFailed);
            ++it;
        } else {
            ++it;
        }
    }
}

SipCallState SipManager::GetCallState(const std::string& call_id) const {
    auto it = calls_.find(call_id);
    return (it != calls_.end()) ? it->second->GetState() : SipCallState::kIdle;
}

SipCall* SipManager::GetCall(const std::string& call_id) {
    auto it = calls_.find(call_id);
    return (it != calls_.end()) ? it->second.get() : nullptr;
}

// ========== Cache & Echo INVITE Headers ==========

void SipManager::CacheLastRequest(const std::string& call_id) {
    last_call_id_ = call_id;
}

std::string SipManager::BuildInviteResponse(int status_code, const std::string& reason,
                                            const std::string& sdp) {
    std::ostringstream m;
    std::string lu = "sip:" + config_.username + "@" + config_.domain;

    m << "SIP/2.0 " << status_code << " " << reason << "\r\n";

    // Echo Via from original request
    m << "Via: SIP/2.0/UDP " << config_.local_addr << ":" << config_.local_port;
    if (!last_via_branch_.empty()) m << ";branch=" << last_via_branch_;
    m << "\r\n";

    // Echo From (with original tag)
    m << "From: <sip:remote@" << config_.domain << ">";
    if (!last_from_tag_.empty()) m << ";tag=" << last_from_tag_;
    m << "\r\n";

    // To (add our tag if 200 OK)
    m << "To: <" << lu << ">";
    if (!last_to_tag_.empty()) m << ";tag=" << last_to_tag_;
    else if (status_code == 200) m << ";tag=" << GenerateTag();
    m << "\r\n";

    // Echo Call-ID
    m << "Call-ID: " << (last_call_id_.empty() ? GenerateCallId() : last_call_id_) << "\r\n";

    // Echo CSeq
    // last_cseq_ already contains method (e.g. "1 INVITE"), don't append " INVITE" again
    m << "CSeq: " << (last_cseq_.empty() ? std::to_string(GenerateCSeq()) + " INVITE" : last_cseq_) << "\r\n";

    m << "Contact: <" << lu << ">\r\n";

    if (!sdp.empty()) {
        m << "Content-Type: application/sdp\r\n";
        m << "Content-Length: " << sdp.size() << "\r\n\r\n";
        m << sdp;
    } else {
        m << "Content-Length: 0\r\n\r\n";
    }
    return m.str();
}

// ========== SDP Negotiation ==========

std::string SipManager::BuildResponseWithSdp(int status_code, const std::string& reason,
                                             const std::string& sdp, const std::string& to_tag) {
    std::ostringstream m;
    std::string lu = "sip:" + config_.username + "@" + config_.domain;
    m << "SIP/2.0 " << status_code << " " << reason << "\r\n";
    m << "Via: SIP/2.0/UDP " << config_.local_addr << ":" << config_.local_port
      << ";branch=" << GenerateBranchId() << "\r\n";
    m << "From: <sip:remote@" << config_.domain << ">;tag=" << GenerateTag() << "\r\n";
    m << "To: <" << lu << ">;tag=" << (to_tag.empty() ? GenerateTag() : to_tag) << "\r\n";
    m << "Call-ID: " << RandomHex(16) << "\r\n";
    m << "CSeq: " << GenerateCSeq() << " INVITE\r\n";
    m << "Contact: <" << lu << ">\r\n";
    m << "Content-Type: application/sdp\r\n";
    m << "Content-Length: " << sdp.size() << "\r\n\r\n";
    m << sdp;
    return m.str();
}

bool SipManager::ProcessIncomingSdp(const std::string& call_id, const std::string& sdp_body) {
    SdpSession sess;
    if (!SdpBuilder::Parse(sdp_body, sess)) {
        LOG(WARNING) << "Failed to parse SDP for call " << call_id;
        return false;
    }

    auto it = calls_.find(call_id);
    if (it == calls_.end()) return false;
    auto& info = it->second->SessionInfo();

    // Extract remote RTP address (c= line) and ports (m= lines)
    info.remote_rtp_addr = sess.conn_addr;

    auto* audio = sess.FindAudio();
    if (audio) {
        info.audio_remote_port = audio->port;
        LOG(INFO) << "SDP: remote audio " << info.remote_rtp_addr << ":" << audio->port;
    }

    auto* video = sess.FindVideo();
    if (video) {
        info.video_remote_port = video->port;
        LOG(INFO) << "SDP: remote video " << info.remote_rtp_addr << ":" << video->port;
    }

    return true;
}

std::string SipManager::BuildSdpAnswer(const std::string& call_id) {
    auto it = calls_.find(call_id);
    if (it == calls_.end()) return "";

    auto& info = it->second->SessionInfo();
    // Our RTP recv ports = send ports + 2 (as opened in GetOrCreateContext)
    return SdpBuilder::BuildAnswer(config_.local_addr,
                                   info.audio_local_port + 2,   // audio recv
                                   info.video_local_port + 2,   // video recv
                                   96, 97);
}

void SipManager::NotifyStateChange(const std::string& call_id, SipCallState state) {
    if (sip_event_cb_) sip_event_cb_(call_id, state);
}

} // namespace rtcom
