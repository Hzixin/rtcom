#pragma once

#include "common/types.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

namespace rtcom {

// RFC 3550 RTP Packet manipulation
class RtpPacket {
public:
    RtpPacket() { memset(&header_, 0, sizeof(header_)); }

    static RtpPacket Parse(const uint8_t* data, size_t len);
    size_t Serialize(uint8_t* buffer, size_t buffer_len) const;
    static RtpPacket Build(uint8_t payload_type, uint16_t seq, uint32_t timestamp,
                           uint32_t ssrc, bool marker, const uint8_t* payload, size_t payload_len);

    const RtpHeader& Header() const { return header_; }
    RtpHeader& MutableHeader() { return header_; }
    const std::vector<uint8_t>& Payload() const { return payload_; }
    std::vector<uint8_t>& MutablePayload() { return payload_; }

    uint16_t Sequence() const { return ntohs(header_.sequence_number); }
    uint32_t Timestamp() const { return ntohl(header_.timestamp); }
    uint32_t Ssrc() const { return ntohl(header_.ssrc); }
    bool Marker() const { return header_.marker; }
    uint8_t PayloadType() const { return header_.payload_type; }

    void SetSequence(uint16_t seq) { header_.sequence_number = htons(seq); }
    void SetTimestamp(uint32_t ts) { header_.timestamp = htonl(ts); }
    void SetSsrc(uint32_t ssrc) { header_.ssrc = htonl(ssrc); }
    void SetMarker(bool m) { header_.marker = m ? 1 : 0; }
    void SetPayloadType(uint8_t pt) { header_.payload_type = pt; }
    void SetPayload(const uint8_t* data, size_t len) { payload_.assign(data, data + len); }

    size_t TotalSize() const { return kRtpHeaderSize + payload_.size(); }
    const uint8_t* PayloadData() const { return payload_.data(); }
    size_t PayloadSize() const { return payload_.size(); }

private:
    RtpHeader header_;
    std::vector<uint8_t> payload_;
};

// H.264 NAL Unit Fragmentation (RFC 6184)
class H264Fragmenter {
public:
    static constexpr size_t kFuIndicatorSize = 1;
    static constexpr size_t kFuHeaderSize = 1;
    static constexpr size_t kMaxFuPayload = kRtpMaxPayloadSize - kRtpHeaderSize - 2;

    static std::vector<std::vector<uint8_t>> Fragment(const uint8_t* nal_data, size_t nal_len, size_t mtu);
    static bool Defragment(const std::vector<std::vector<uint8_t>>& fragments,
                           std::vector<uint8_t>& complete_nal);
};

// AAC packetization helper (RFC 3640 / RFC 6416)
class AacPacketizer {
public:
    static std::vector<uint8_t> Pack(const uint8_t* aac_data, size_t aac_len);
    static bool Unpack(const uint8_t* rtp_payload, size_t payload_len,
                       std::vector<uint8_t>& aac_data);
};

} // namespace rtcom
