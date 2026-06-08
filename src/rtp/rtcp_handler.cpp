#include "rtcp_handler.h"
#include <cstring>
#include <chrono>
#include <glog/logging.h>
#include <arpa/inet.h>

namespace rtcom {

RtcpHandler::RtcpHandler(uint32_t ssrc) : ssrc_(ssrc) {
    last_rtcp_send_ = std::chrono::steady_clock::now();
}

std::vector<uint8_t> RtcpHandler::BuildSenderReport(
    uint64_t ntp_ts, uint32_t rtp_ts,
    uint32_t packet_count, uint32_t octet_count) {

    std::vector<uint8_t> packet(28);
    auto* sr = reinterpret_cast<RtcpSRHeader*>(packet.data());
    sr->version = kRtpVersion;
    sr->padding = 0;
    sr->report_count = 0;
    sr->packet_type = 200;
    sr->length = htons(6);
    sr->ssrc = htonl(ssrc_);
    sr->ntp_timestamp = ((static_cast<uint64_t>(htonl(ntp_ts >> 32))) << 32) | htonl(ntp_ts & 0xFFFFFFFF);
    sr->rtp_timestamp = htonl(rtp_ts);
    sr->packet_count = htonl(packet_count);
    sr->octet_count = htonl(octet_count);
    return packet;
}

std::vector<uint8_t> RtcpHandler::BuildReceiverReport(
    const std::vector<RtcpReportBlock>& blocks) {

    size_t header_size = 8;
    size_t total = header_size + blocks.size() * 24;
    std::vector<uint8_t> packet(total);
    packet[0] = (kRtpVersion << 6) | (blocks.size() & 0x1F);
    packet[1] = 201;
    uint16_t len = static_cast<uint16_t>((total / 4) - 1);
    packet[2] = (len >> 8) & 0xFF;
    packet[3] = len & 0xFF;
    *reinterpret_cast<uint32_t*>(&packet[4]) = htonl(ssrc_);

    for (size_t i = 0; i < blocks.size(); ++i) {
        size_t offset = header_size + i * 24;
        auto* rb = reinterpret_cast<RtcpReportBlock*>(&packet[offset]);
        rb->ssrc = htonl(blocks[i].ssrc);
        rb->fraction_lost = blocks[i].fraction_lost;
        rb->cumulative_lost = htonl(blocks[i].cumulative_lost) >> 8;
        rb->highest_seqnum = htonl(blocks[i].highest_seqnum);
        rb->interarrival_jitter = htonl(blocks[i].interarrival_jitter);
        rb->last_sr = htonl(blocks[i].last_sr);
        rb->delay_since_last_sr = htonl(blocks[i].delay_since_last_sr);
    }
    return packet;
}

std::vector<uint8_t> RtcpHandler::BuildSdes(const std::string& cname) {
    size_t cname_len = cname.length();
    size_t item_len = 1 + 1 + 1 + cname_len;
    size_t total = 4 + item_len;
    size_t padded = (total + 3) & ~3;

    std::vector<uint8_t> packet(padded, 0);
    packet[0] = (kRtpVersion << 6) | 1;
    packet[1] = 202;
    uint16_t len = static_cast<uint16_t>((padded / 4) - 1);
    packet[2] = (len >> 8) & 0xFF;
    packet[3] = len & 0xFF;
    *reinterpret_cast<uint32_t*>(&packet[4]) = htonl(ssrc_);
    packet[8] = 1;
    packet[9] = static_cast<uint8_t>(cname_len);
    memcpy(&packet[10], cname.c_str(), cname_len);
    return packet;
}

std::vector<uint8_t> RtcpHandler::BuildCompound(
    const std::string& cname, uint64_t ntp_ts, uint32_t rtp_ts,
    uint32_t packet_count, uint32_t octet_count) {
    auto sr = BuildSenderReport(ntp_ts, rtp_ts, packet_count, octet_count);
    auto sdes = BuildSdes(cname);
    std::vector<uint8_t> compound;
    compound.insert(compound.end(), sr.begin(), sr.end());
    compound.insert(compound.end(), sdes.begin(), sdes.end());
    return compound;
}

size_t RtcpHandler::Parse(const uint8_t* data, size_t len,
                          std::vector<RtcpReportBlock>& blocks,
                          uint32_t& reporter_ssrc) {
    if (len < 8) return 0;
    size_t offset = 0, count = 0;
    while (offset + 4 <= len) {
        uint8_t pt = data[offset + 1];
        uint16_t pkt_len = (static_cast<uint16_t>(data[offset + 2]) << 8) | data[offset + 3];
        size_t pkt_bytes = (pkt_len + 1) * 4;
        if (offset + pkt_bytes > len) break;
        if (pt == 200 || pt == 201) {
            reporter_ssrc = ntohl(*reinterpret_cast<const uint32_t*>(&data[offset + 4]));
            uint8_t rc = data[offset] & 0x1F;
            size_t rb_offset = offset + 8;
            for (uint8_t i = 0; i < rc && rb_offset + 24 <= offset + pkt_bytes; ++i) {
                RtcpReportBlock block;
                memcpy(&block, &data[rb_offset], 24);
                block.ssrc = ntohl(block.ssrc);
                block.highest_seqnum = ntohl(block.highest_seqnum);
                block.interarrival_jitter = ntohl(block.interarrival_jitter);
                block.last_sr = ntohl(block.last_sr);
                block.delay_since_last_sr = ntohl(block.delay_since_last_sr);
                blocks.push_back(block);
                rb_offset += 24;
                count++;
            }
        }
        offset += pkt_bytes;
    }
    return count;
}

uint64_t RtcpHandler::NowNtp() {
    constexpr uint64_t kNtpEpochOffset = 2208988800ULL;
    auto now = std::chrono::system_clock::now();
    auto since_epoch = now.time_since_epoch();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(since_epoch).count();
    auto frac = std::chrono::duration_cast<std::chrono::nanoseconds>(
        since_epoch - std::chrono::seconds(secs)).count();
    uint64_t ntp_sec = static_cast<uint64_t>(secs) + kNtpEpochOffset;
    uint64_t ntp_frac = static_cast<uint64_t>((static_cast<double>(frac) / 1e9) * (1ULL << 32));
    return (ntp_sec << 32) | ntp_frac;
}

void RtcpHandler::UpdateFromReport(const RtcpReportBlock& block) {
    fraction_lost_ = block.fraction_lost;
    cumulative_lost_ = block.cumulative_lost;
    highest_seq_ = block.highest_seqnum;
    jitter_ = block.interarrival_jitter;
    last_sr_ts_ = block.last_sr;
    delay_since_last_sr_ = block.delay_since_last_sr;
}

} // namespace rtcom
