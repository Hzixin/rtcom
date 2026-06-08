#include "jitter_buffer.h"
#include <glog/logging.h>
#include <cmath>
#include <algorithm>

namespace rtcom {

JitterBuffer::JitterBuffer(const JitterBufferConfig& config)
    : config_(config), current_delay_ms_(config.initial_delay_ms) {}

JitterBuffer::~JitterBuffer() = default;

void JitterBuffer::CalculateJitter(const RtpPacket& packet,
                                   std::chrono::steady_clock::time_point arrival) {
    uint32_t rtp_ts = packet.Timestamp();
    if (!jitter_initialized_) {
        last_transit_ = 0;
        transit_jitter_ = 0;
        jitter_initialized_ = true;
        return;
    }
    auto arrival_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        arrival.time_since_epoch()).count();
    int32_t transit = static_cast<int32_t>(arrival_ms) -
                      static_cast<int32_t>((static_cast<uint64_t>(rtp_ts) * 1000) / config_.sample_rate);
    int32_t delta = std::abs(transit - static_cast<int32_t>(last_transit_));
    transit_jitter_ += (static_cast<uint32_t>(delta) - transit_jitter_) / 16;
    last_transit_ = static_cast<uint32_t>(transit);
    current_jitter_ms_ = transit_jitter_;
}

void JitterBuffer::SortBuffer() {
    std::sort(buffer_.begin(), buffer_.end(),
              [](const BufferItem& a, const BufferItem& b) {
                  return a.packet.Sequence() < b.packet.Sequence();
              });
}

bool JitterBuffer::Insert(RtpPacket&& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    CalculateJitter(packet, now);
    uint16_t seq = packet.Sequence();

    if (!initialized_) { expected_seq_ = seq; initialized_ = true; }

    uint16_t gap = seq - expected_seq_;
    if (gap > 0 && gap < 100) packets_lost_ += gap;
    if (static_cast<int16_t>(seq - expected_seq_) >= 0) expected_seq_ = seq + 1;

    BufferItem item;
    item.packet = std::move(packet);
    item.arrival_time = now;
    item.ready = false;
    buffer_.push_back(std::move(item));
    packets_inserted_++;
    SortBuffer();

    // Mark ready packets
    if (!buffer_.empty()) {
        uint32_t buffer_depth = GetBufferDepthMs();
        for (auto& bi : buffer_) {
            auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - bi.arrival_time).count();
            bi.ready = (static_cast<uint32_t>(wait_ms) >= current_delay_ms_);
        }
        if (current_jitter_ms_ > current_delay_ms_) {
            current_delay_ms_ = std::min(config_.max_delay_ms, current_delay_ms_ + 10);
        } else if (current_delay_ms_ > config_.min_delay_ms && buffer_depth < current_delay_ms_ / 2) {
            current_delay_ms_ = std::max(config_.min_delay_ms, current_delay_ms_ - 5);
        }
    }

    while (buffer_.size() > kMaxBufferSize) {
        buffer_.pop_front(); packets_lost_++;
    }
    return true;
}

RtpPacket* JitterBuffer::Extract() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (buffer_.empty()) return nullptr;
    for (auto it = buffer_.begin(); it != buffer_.end(); ++it) {
        if (it->ready) {
            static thread_local RtpPacket extracted;
            extracted = std::move(it->packet);
            buffer_.erase(it); packets_extracted_++;
            return &extracted;
        }
    }
    return nullptr;
}

bool JitterBuffer::Empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.empty();
}

uint32_t JitterBuffer::GetBufferDepthMs() const {
    if (buffer_.size() < 2) return 0;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        buffer_.back().arrival_time - buffer_.front().arrival_time).count();
}

void JitterBuffer::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    initialized_ = false;
    expected_seq_ = 0;
    current_delay_ms_ = config_.initial_delay_ms;
    packets_lost_ = 0;
    jitter_initialized_ = false;
}

} // namespace rtcom
