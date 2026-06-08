#include "rtp_packet.h"
#include <algorithm>
#include <glog/logging.h>

namespace rtcom {

RtpPacket RtpPacket::Parse(const uint8_t* data, size_t len) {
    RtpPacket packet;
    if (len < kRtpHeaderSize) {
        LOG(WARNING) << "RTP packet too short: " << len << " bytes";
        return packet;
    }

    memcpy(&packet.header_, data, kRtpHeaderSize);

    size_t payload_offset = kRtpHeaderSize + (packet.header_.csrc_count * 4);
    if (packet.header_.extension && len > payload_offset + 4) {
        uint16_t ext_len = ntohs(*reinterpret_cast<const uint16_t*>(data + payload_offset + 2));
        payload_offset += 4 + ext_len * 4;
    }

    if (payload_offset < len) {
        size_t payload_len = len - payload_offset;
        if (packet.header_.padding && payload_len > 0) {
            uint8_t pad_len = data[len - 1];
            if (pad_len < payload_len) payload_len -= pad_len;
        }
        packet.payload_.assign(data + payload_offset, data + payload_offset + payload_len);
    }
    return packet;
}

size_t RtpPacket::Serialize(uint8_t* buffer, size_t buffer_len) const {
    size_t total = TotalSize();
    if (buffer_len < total) return 0;
    memcpy(buffer, &header_, kRtpHeaderSize);
    if (!payload_.empty()) {
        memcpy(buffer + kRtpHeaderSize, payload_.data(), payload_.size());
    }
    return total;
}

RtpPacket RtpPacket::Build(uint8_t payload_type, uint16_t seq, uint32_t timestamp,
                           uint32_t ssrc, bool marker, const uint8_t* payload, size_t payload_len) {
    RtpPacket packet;
    packet.header_.version = kRtpVersion;
    packet.header_.csrc_count = 0;
    packet.header_.extension = 0;
    packet.header_.padding = 0;
    packet.header_.marker = marker ? 1 : 0;
    packet.header_.payload_type = payload_type;
    packet.header_.sequence_number = htons(seq);
    packet.header_.timestamp = htonl(timestamp);
    packet.header_.ssrc = htonl(ssrc);
    if (payload && payload_len > 0) {
        packet.payload_.assign(payload, payload + payload_len);
    }
    return packet;
}

// ---- H.264 FU-A Fragmenter ----

std::vector<std::vector<uint8_t>> H264Fragmenter::Fragment(
    const uint8_t* nal_data, size_t nal_len, size_t mtu) {

    std::vector<std::vector<uint8_t>> fragments;
    if (nal_len <= mtu) {
        fragments.emplace_back(nal_data, nal_data + nal_len);
        return fragments;
    }

    uint8_t nal_header = nal_data[0];
    uint8_t fu_indicator = (nal_header & 0xE0) | 28;  // NRI + FU-A type

    const uint8_t* nal_payload = nal_data + 1;
    size_t nal_payload_len = nal_len - 1;
    size_t offset = 0;
    bool first = true;

    while (offset < nal_payload_len) {
        size_t chunk = std::min(kMaxFuPayload, nal_payload_len - offset);

        uint8_t fu_header = nal_header & 0x1F;
        if (first) { fu_header |= 0x80; first = false; }
        if (offset + chunk >= nal_payload_len) fu_header |= 0x40;

        std::vector<uint8_t> frag;
        frag.push_back(fu_indicator);
        frag.push_back(fu_header);
        frag.insert(frag.end(), nal_payload + offset, nal_payload + offset + chunk);
        fragments.push_back(std::move(frag));

        offset += chunk;
    }
    return fragments;
}

bool H264Fragmenter::Defragment(const std::vector<std::vector<uint8_t>>& fragments,
                                std::vector<uint8_t>& complete_nal) {
    if (fragments.empty()) return false;
    complete_nal.clear();

    for (size_t i = 0; i < fragments.size(); ++i) {
        const auto& frag = fragments[i];
        if (frag.size() < 2) continue;

        uint8_t fu_indicator = frag[0];
        uint8_t fu_header = frag[1];
        uint8_t nal_header = (fu_indicator & 0xE0) | (fu_header & 0x1F);

        if (i == 0) complete_nal.push_back(nal_header);
        complete_nal.insert(complete_nal.end(), frag.begin() + 2, frag.end());
    }
    return true;
}

// ---- AAC Packetizer ----

std::vector<uint8_t> AacPacketizer::Pack(const uint8_t* aac_data, size_t aac_len) {
    std::vector<uint8_t> rtp_payload;
    uint16_t au_header_len = 0;  // bits
    uint16_t au_size = static_cast<uint16_t>(aac_len);

    // AU-header-length (2 bytes)
    rtp_payload.push_back(static_cast<uint8_t>((au_header_len >> 8) & 0xFF));
    rtp_payload.push_back(static_cast<uint8_t>(au_header_len & 0xFF));
    // AU-header: AU-size (13 bits)
    rtp_payload.push_back(static_cast<uint8_t>((au_size >> 5) & 0xFF));
    rtp_payload.push_back(static_cast<uint8_t>((au_size & 0x1F) << 3));
    // AAC data
    rtp_payload.insert(rtp_payload.end(), aac_data, aac_data + aac_len);
    return rtp_payload;
}

bool AacPacketizer::Unpack(const uint8_t* rtp_payload, size_t payload_len,
                           std::vector<uint8_t>& aac_data) {
    if (payload_len < 4) return false;
    uint16_t au_size = ((static_cast<uint16_t>(rtp_payload[2]) << 5) |
                        (rtp_payload[3] >> 3)) & 0x1FFF;
    size_t au_data_offset = 2 + (((static_cast<uint16_t>(rtp_payload[0]) << 8) | rtp_payload[1]) / 8);
    if (au_data_offset + au_size > payload_len) return false;
    aac_data.assign(rtp_payload + au_data_offset, rtp_payload + au_data_offset + au_size);
    return true;
}

} // namespace rtcom
