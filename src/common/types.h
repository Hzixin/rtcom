#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <chrono>
#include <vector>
#include <map>

namespace rtcom {

// ============================================================================
// SIP Types
// ============================================================================

enum class SipCallState {
    kIdle,
    kCalling,           // INVITE sent, waiting for response
    kRinging,           // 180 Ringing received
    kInProgress,        // 183 Session Progress
    kConnected,         // 200 OK / ACK exchange complete
    kDisconnecting,     // BYE sent
    kDisconnected,      // BYE acknowledged
    kFailed,            // Call failed
    kRegistered,        // REGISTER complete
    kUnregistered       // Not registered
};

enum class SipMethod {
    kRegister,
    kInvite,
    kAck,
    kBye,
    kCancel,
    kOptions
};

// ============================================================================
// Media Types
// ============================================================================

enum class CodecType {
    kAudioAAC,      // AAC audio
    kVideoH264,     // H.264 video
    kAudioPCMU,     // G.711 u-law
    kAudioPCMA      // G.711 A-law
};

enum class MediaDirection {
    kSendOnly,
    kRecvOnly,
    kSendRecv,
    kInactive
};

// ============================================================================
// RTP Types
// ============================================================================

constexpr uint8_t kRtpVersion = 2;
constexpr size_t kRtpHeaderSize = 12;
constexpr size_t kRtpMaxPayloadSize = 1460;    // Ethernet MTU safe
constexpr size_t kRtcpMaxPacketSize = 1500;
constexpr uint16_t kDefaultAudioPort = 5004;
constexpr uint16_t kDefaultVideoPort = 5010;
constexpr uint32_t kJitterBufferMaxMs = 200;    // Max jitter buffer in ms
constexpr uint32_t kJitterBufferMinMs = 20;     // Min jitter buffer in ms

enum class RtpPayloadType : uint8_t {
    kPCMU = 0,
    kPCMA = 8,
    kAAC  = 96,     // Dynamic
    kH264 = 97      // Dynamic
};

// RTP Header (RFC 3550 Section 5.1)
#pragma pack(push, 1)
struct RtpHeader {
    uint8_t  csrc_count : 4;
    uint8_t  extension  : 1;
    uint8_t  padding    : 1;
    uint8_t  version    : 2;
    uint8_t  payload_type : 7;
    uint8_t  marker       : 1;
    uint16_t sequence_number;
    uint32_t timestamp;
    uint32_t ssrc;
    // CSRC list follows (0-15 items)
    // Extension header follows (if extension bit set)
};

// RTCP Sender Report (RFC 3550 Section 6.4.1)
struct RtcpSRHeader {
    uint8_t  report_count : 5;
    uint8_t  padding      : 1;
    uint8_t  version      : 2;
    uint8_t  packet_type;      // 200 for SR
    uint16_t length;           // in 32-bit words - 1
    uint32_t ssrc;
    uint64_t ntp_timestamp;
    uint32_t rtp_timestamp;
    uint32_t packet_count;
    uint32_t octet_count;
};

// RTCP Receiver Report Block (RFC 3550 Section 6.4.2)
struct RtcpReportBlock {
    uint32_t ssrc;
    uint8_t  fraction_lost;
    uint32_t cumulative_lost : 24;
    uint32_t highest_seqnum;
    uint32_t interarrival_jitter;
    uint32_t last_sr;
    uint32_t delay_since_last_sr;
};
#pragma pack(pop)

// ============================================================================
// Configuration
// ============================================================================

struct SipConfig {
    std::string server_addr = "127.0.0.1";
    uint16_t   server_port = 5060;
    std::string local_addr  = "127.0.0.1";
    uint16_t   local_port   = 5060;
    std::string username    = "user";
    std::string domain      = "example.com";
    std::string password;
    uint32_t   register_expires = 3600;
    uint32_t   retry_t1_ms     = 500;     // SIP T1 timer
    uint32_t   retry_t2_ms     = 4000;    // SIP T2 timer
    uint32_t   retry_t4_ms     = 5000;    // SIP T4 timer
    uint32_t   max_retransmit  = 7;
};

struct MediaConfig {
    uint16_t audio_port  = kDefaultAudioPort;
    uint16_t video_port  = kDefaultVideoPort;
    CodecType audio_codec = CodecType::kAudioAAC;
    CodecType video_codec = CodecType::kVideoH264;
    uint32_t audio_bitrate = 64000;      // bps
    uint32_t video_bitrate = 1000000;    // bps
    uint32_t audio_sample_rate = 44100;
    uint32_t video_fps    = 30;
    uint32_t video_width  = 640;
    uint32_t video_height = 480;
};

struct ServerConfig {
    SipConfig   sip;
    MediaConfig media;
    uint32_t    max_sessions   = 100;
    uint32_t    worker_threads = 4;
    bool        use_glog       = true;
    std::string log_dir        = "./logs";
};

// ============================================================================
// Call Session Info
// ============================================================================

struct CallSessionInfo {
    std::string call_id;
    std::string caller_uri;
    std::string callee_uri;
    SipCallState state = SipCallState::kIdle;
    uint32_t   local_ssrc;
    uint32_t   remote_ssrc;
    uint32_t   rtp_timestamp_base;
    uint16_t   rtp_seq_base;
    uint16_t   audio_local_port;
    uint16_t   video_local_port;
    uint16_t   audio_remote_port;
    uint16_t   video_remote_port;
    std::string remote_rtp_addr;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_rtp_time;
};

// Callback types
using SipEventCallback = std::function<void(const std::string& call_id, SipCallState state)>;
using RtpPacketCallback = std::function<void(const std::string& call_id,
    const uint8_t* data, size_t len, RtpPayloadType pt)>;
using ErrorCallback = std::function<void(const std::string& error)>;

// ============================================================================
// Utility: Get string representation
// ============================================================================

inline const char* SipCallStateToString(SipCallState s) {
    switch (s) {
        case SipCallState::kIdle:          return "Idle";
        case SipCallState::kCalling:       return "Calling";
        case SipCallState::kRinging:       return "Ringing";
        case SipCallState::kInProgress:    return "InProgress";
        case SipCallState::kConnected:     return "Connected";
        case SipCallState::kDisconnecting: return "Disconnecting";
        case SipCallState::kDisconnected:  return "Disconnected";
        case SipCallState::kFailed:        return "Failed";
        case SipCallState::kRegistered:    return "Registered";
        case SipCallState::kUnregistered:  return "Unregistered";
    }
    return "Unknown";
}

inline const char* CodecTypeToString(CodecType c) {
    switch (c) {
        case CodecType::kAudioAAC:  return "AAC";
        case CodecType::kVideoH264: return "H264";
        case CodecType::kAudioPCMU: return "PCMU";
        case CodecType::kAudioPCMA: return "PCMA";
    }
    return "Unknown";
}

} // namespace rtcom
