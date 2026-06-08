#pragma once

#include "rtp_packet.h"
#include <deque>
#include <mutex>
#include <chrono>

namespace rtcom {

struct JitterBufferConfig {
    uint32_t min_delay_ms = kJitterBufferMinMs;
    uint32_t max_delay_ms = kJitterBufferMaxMs;
    uint32_t initial_delay_ms = 60;
    uint32_t sample_rate = 8000;
};

class JitterBuffer {
public:
    explicit JitterBuffer(const JitterBufferConfig& config = JitterBufferConfig());
    ~JitterBuffer();

    bool Insert(RtpPacket&& packet);
    RtpPacket* Extract();
    bool Empty() const;
    uint32_t GetBufferDepthMs() const;

    uint32_t PacketsLost() const { return packets_lost_; }
    uint32_t PacketsInserted() const { return packets_inserted_; }
    uint32_t PacketsExtracted() const { return packets_extracted_; }
    uint32_t GetCurrentJitterMs() const { return current_jitter_ms_; }

    void Reset();

private:
    struct BufferItem {
        RtpPacket packet;
        std::chrono::steady_clock::time_point arrival_time;
        bool ready;
    };

    JitterBufferConfig config_;
    std::deque<BufferItem> buffer_;
    mutable std::mutex mutex_;

    uint16_t expected_seq_{0};
    bool initialized_{false};
    uint32_t current_delay_ms_;

    uint32_t packets_lost_{0};
    uint32_t packets_inserted_{0};
    uint32_t packets_extracted_{0};
    uint32_t current_jitter_ms_{0};
    uint32_t transit_jitter_{0};
    uint32_t last_transit_{0};
    bool jitter_initialized_{false};

    void CalculateJitter(const RtpPacket& packet,
                         std::chrono::steady_clock::time_point arrival);
    void SortBuffer();

    static constexpr size_t kMaxBufferSize = 500;
};

} // namespace rtcom
