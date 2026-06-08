#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace rtcom {

// One media line (m=) in SDP
struct SdpMedia {
    std::string type;         // "audio" or "video"
    uint16_t   port = 0;      // media port
    std::string protocol;     // "RTP/AVP"
    std::vector<int> formats; // payload types (0=PCMU, 96=AAC, 97=H264)
    std::string rtpmap;       // a=rtpmap:...
};

// Parsed SDP session (RFC 4566)
struct SdpSession {
    std::string conn_addr;    // c= IP address
    std::vector<SdpMedia> media; // m= lines

    const SdpMedia* FindAudio() const;
    const SdpMedia* FindVideo() const;
};

class SdpBuilder {
public:
    // Parse SDP text → structured object
    static bool Parse(const std::string& sdp_text, SdpSession& out);

    // Build a standard SDP offer (outgoing call)
    static std::string BuildOffer(const std::string& local_ip,
                                  uint16_t audio_port, uint16_t video_port,
                                  int audio_pt = 96, int video_pt = 97);

    // Build a standard SDP answer (respond to offer)
    static std::string BuildAnswer(const std::string& local_ip,
                                   uint16_t audio_port, uint16_t video_port,
                                   int audio_pt = 96, int video_pt = 97);
};

} // namespace rtcom
