#pragma once

#include "common/types.h"
#include <vector>
#include <cstdint>
#include <chrono>

namespace rtcom {

class RtcpHandler {
public:
    explicit RtcpHandler(uint32_t ssrc);

    std::vector<uint8_t> BuildSenderReport(uint64_t ntp_ts, uint32_t rtp_ts,
                                           uint32_t packet_count, uint32_t octet_count);
    std::vector<uint8_t> BuildReceiverReport(const std::vector<RtcpReportBlock>& blocks);
    std::vector<uint8_t> BuildSdes(const std::string& cname);
    std::vector<uint8_t> BuildCompound(const std::string& cname,
                                       uint64_t ntp_ts, uint32_t rtp_ts,
                                       uint32_t packet_count, uint32_t octet_count);

    static size_t Parse(const uint8_t* data, size_t len,
                        std::vector<RtcpReportBlock>& blocks, uint32_t& reporter_ssrc);
    static uint64_t NowNtp();

    void UpdateFromReport(const RtcpReportBlock& block);

    uint8_t  FractionLost() const { return fraction_lost_; }
    uint32_t CumulativeLost() const { return cumulative_lost_; }
    uint32_t Jitter() const { return jitter_; }
    uint32_t LastSrTimestamp() const { return last_sr_ts_; }

    static constexpr double kRtcpIntervalSec = 5.0;

private:
    uint32_t ssrc_;
    uint8_t  fraction_lost_{0};
    uint32_t cumulative_lost_{0};
    uint32_t highest_seq_{0};
    uint32_t jitter_{0};
    uint32_t last_sr_ts_{0};
    uint32_t delay_since_last_sr_{0};
    std::chrono::steady_clock::time_point last_rtcp_send_;
};

} // namespace rtcom
