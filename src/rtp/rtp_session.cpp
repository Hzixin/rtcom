#include "rtp_session.h"
#include <glog/logging.h>
#include <algorithm>

namespace rtcom {

std::mt19937 RtpSession::rng_(std::random_device{}());

uint32_t RtpSession::GenerateSsrc() {
    std::uniform_int_distribution<uint32_t> dist;
    return dist(rng_);
}

RtpSession::RtpSession(uint32_t ssrc, RtpPayloadType payload_type)
    : ssrc_(ssrc ? ssrc : GenerateSsrc()), payload_type_(payload_type) {
    current_timestamp_ = std::uniform_int_distribution<uint32_t>()(rng_);
    LOG(INFO) << "RTP session: SSRC=0x" << std::hex << ssrc_
              << " PT=" << static_cast<int>(payload_type_);
}

RtpSession::~RtpSession() {
    LOG(INFO) << "RTP session closed: sent=" << packets_sent_
              << " recv=" << packets_received_;
}

bool RtpSession::OpenSendSocket(uint16_t local_port) {
    send_socket_ = std::make_unique<UdpSocket>();
    if (!send_socket_->Create()) return false;
    send_socket_->SetNonBlocking(true);
    send_socket_->SetReuseAddr(true);
    send_socket_->SetSendBuffer(256 * 1024);
    return send_socket_->Bind(local_port);
}

bool RtpSession::OpenRecvSocket(uint16_t local_port) {
    recv_socket_ = std::make_unique<UdpSocket>();
    if (!recv_socket_->Create()) return false;
    recv_socket_->SetNonBlocking(true);
    recv_socket_->SetReuseAddr(true);
    recv_socket_->SetRecvBuffer(256 * 1024);
    return recv_socket_->Bind(local_port);
}

bool RtpSession::SendPacket(const uint8_t* payload, size_t payload_len,
                            const std::string& dest_addr, uint16_t dest_port,
                            bool marker) {
    if (!send_socket_) return false;
    const std::string& dst = dest_addr.empty() ? remote_addr_ : dest_addr;
    uint16_t dport = dest_port ? dest_port : remote_port_;
    if (dst.empty() || dport == 0) return false;

    auto packet = RtpPacket::Build(static_cast<uint8_t>(payload_type_),
                                   current_seq_, current_timestamp_, ssrc_,
                                   marker, payload, payload_len);

    uint8_t buffer[kRtpMaxPayloadSize + kRtpHeaderSize];
    size_t total = packet.Serialize(buffer, sizeof(buffer));
    if (total == 0) return false;

    ssize_t sent = send_socket_->SendTo(buffer, total, dst, dport);
    if (sent < 0) return false;

    current_seq_++;
    packets_sent_++;
    bytes_sent_ += total;
    return true;
}

int RtpSession::ReceivePacket() {
    if (!recv_socket_) return -1;

    uint8_t buffer[65536];
    std::string src_addr;
    uint16_t src_port;

    ssize_t received = recv_socket_->RecvFrom(buffer, sizeof(buffer), src_addr, src_port);
    if (received <= 0) return 0;

    if (remote_addr_.empty()) {
        remote_addr_ = src_addr;
        remote_port_ = src_port;
    }

    last_packet_ = RtpPacket::Parse(buffer, static_cast<size_t>(received));
    packets_received_++;
    bytes_received_ += received;

    uint16_t seq = last_packet_.Sequence();
    received_seq_set_.insert(seq);
    last_received_seq_ = seq;
    return 1;
}

uint16_t RtpSession::LocalSendPort() const {
    return send_socket_ ? send_socket_->GetLocalPort() : 0;
}

uint16_t RtpSession::LocalRecvPort() const {
    return recv_socket_ ? recv_socket_->GetLocalPort() : 0;
}

void RtpSession::AdvanceTimestamp(uint32_t increment) {
    current_timestamp_ += increment;
}

} // namespace rtcom
