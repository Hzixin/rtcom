#include "sdp.h"
#include <sstream>
#include <glog/logging.h>

namespace rtcom {

const SdpMedia* SdpSession::FindAudio() const {
    for (auto& m : media) if (m.type == "audio") return &m;
    return nullptr;
}

const SdpMedia* SdpSession::FindVideo() const {
    for (auto& m : media) if (m.type == "video") return &m;
    return nullptr;
}

// ============================================================================
// Parse SDP text → SdpSession
// ============================================================================

bool SdpBuilder::Parse(const std::string& sdp_text, SdpSession& out) {
    std::istringstream ss(sdp_text);
    std::string line;
    SdpMedia* cur_media = nullptr;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // c=IN IP4 192.168.1.1
        if (line.rfind("c=", 0) == 0) {
            std::istringstream ls(line.substr(2));
            std::string nettype, addrtype, addr;
            ls >> nettype >> addrtype >> addr;
            if (!addr.empty()) out.conn_addr = addr;
            continue;
        }

        // m=audio 5004 RTP/AVP 96
        if (line.rfind("m=", 0) == 0) {
            out.media.emplace_back();
            cur_media = &out.media.back();
            std::istringstream ls(line.substr(2));
            ls >> cur_media->type >> cur_media->port >> cur_media->protocol;
            int fmt;
            while (ls >> fmt) cur_media->formats.push_back(fmt);
            continue;
        }

        // a=rtpmap:96 AAC/44100/1
        if (line.rfind("a=rtpmap:", 0) == 0 && cur_media) {
            cur_media->rtpmap = line.substr(2);
            continue;
        }
    }
    return !out.media.empty();
}

// ============================================================================
// Build SDP Offer (for outgoing INVITE) — advertises OUR ports
// ============================================================================

std::string SdpBuilder::BuildOffer(const std::string& local_ip,
                                   uint16_t audio_port, uint16_t video_port,
                                   int audio_pt, int video_pt) {
    std::ostringstream s;
    s << "v=0\r\n";
    s << "o=rtcom 1 1 IN IP4 " << local_ip << "\r\n";
    s << "s=rtcom\r\n";
    s << "c=IN IP4 " << local_ip << "\r\n";
    s << "t=0 0\r\n";
    s << "m=audio " << audio_port << " RTP/AVP " << audio_pt << "\r\n";
    s << "a=rtpmap:" << audio_pt << " AAC/44100/1\r\n";
    s << "m=video " << video_port << " RTP/AVP " << video_pt << "\r\n";
    s << "a=rtpmap:" << video_pt << " H264/90000\r\n";
    return s.str();
}

// ============================================================================
// Build SDP Answer (for 200 OK) — responds with OUR ports
// ============================================================================

std::string SdpBuilder::BuildAnswer(const std::string& local_ip,
                                    uint16_t audio_port, uint16_t video_port,
                                    int audio_pt, int video_pt) {
    std::ostringstream s;
    s << "v=0\r\n";
    s << "o=rtcom 1 1 IN IP4 " << local_ip << "\r\n";
    s << "s=rtcom\r\n";
    s << "c=IN IP4 " << local_ip << "\r\n";
    s << "t=0 0\r\n";
    s << "m=audio " << audio_port << " RTP/AVP " << audio_pt << "\r\n";
    s << "a=rtpmap:" << audio_pt << " AAC/44100/1\r\n";
    s << "m=video " << video_port << " RTP/AVP " << video_pt << "\r\n";
    s << "a=rtpmap:" << video_pt << " H264/90000\r\n";
    return s.str();
}

} // namespace rtcom
