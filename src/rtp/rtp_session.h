#pragma once

#include "common/types.h"
#include "rtp_packet.h"
#include "net/udp_socket.h"
#include <memory>
#include <random>
#include <set>

namespace rtcom {

class RtpSession {
public:
    RtpSession(uint32_t ssrc, RtpPayloadType payload_type);
    ~RtpSession();

    bool OpenSendSocket(uint16_t local_port);
    bool OpenRecvSocket(uint16_t local_port);

    bool SendPacket(const uint8_t* payload, size_t payload_len,
                    const std::string& dest_addr, uint16_t dest_port,
                    bool marker = false);
    int  ReceivePacket();

    const RtpPacket& GetLastPacket() const { return last_packet_; }

    uint32_t PacketsSent() const { return packets_sent_; }
    uint32_t PacketsReceived() const { return packets_received_; }
    uint64_t BytesSent() const { return bytes_sent_; }
    uint64_t BytesReceived() const { return bytes_received_; }
    uint16_t CurrentSeq() const { return current_seq_; }
    uint32_t CurrentTimestamp() const { return current_timestamp_; }
    uint32_t Ssrc() const { return ssrc_; }

    void SetRemoteAddr(const std::string& addr) { remote_addr_ = addr; }
    void SetRemotePort(uint16_t port) { remote_port_ = port; }
    std::string RemoteAddr() const { return remote_addr_; }
    uint16_t RemotePort() const { return remote_port_; }

    int SendFd() const { return send_socket_ ? send_socket_->GetFd() : -1; }
    int RecvFd() const { return recv_socket_ ? recv_socket_->GetFd() : -1; }

    uint16_t LocalSendPort() const;
    uint16_t LocalRecvPort() const;

    void AdvanceTimestamp(uint32_t increment);
    void SetTimestampBase(uint32_t base) { timestamp_base_ = base; }
    void SetTimestampIncrement(uint32_t inc) { timestamp_inc_ = inc; }

private:
    static uint32_t GenerateSsrc();
    static std::mt19937 rng_;

    uint32_t ssrc_;
    RtpPayloadType payload_type_;
    uint32_t timestamp_base_{0};
    uint32_t timestamp_inc_{160};

    uint16_t current_seq_{0};
    uint32_t current_timestamp_{0};

    std::unique_ptr<UdpSocket> send_socket_;
    std::unique_ptr<UdpSocket> recv_socket_;

    std::string remote_addr_;
    uint16_t remote_port_{0};

    RtpPacket last_packet_;

    uint32_t packets_sent_{0};
    uint32_t packets_received_{0};
    uint64_t bytes_sent_{0};
    uint64_t bytes_received_{0};

    uint16_t last_received_seq_{0};
    std::set<uint16_t> received_seq_set_;
};

} // namespace rtcom
