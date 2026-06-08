#include <glog/logging.h>
#undef WARNING

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>

#include "common/config.h"
#include "sip/sip_manager.h"
#include "rtp/rtp_packet.h"
#include "rtp/rtp_session.h"
#include "session/session_manager.h"
#include "net/udp_socket.h"

using namespace rtcom;

static std::atomic<bool> g_running{true};

void SignalHandler(int sig) {
    LOG(INFO) << "Received signal " << sig << ", shutting down...";
    g_running = false;
}

void TestRtpPacket() {
    LOG(INFO) << "=== RTP Packet Self-Test ===";
    uint8_t payload[] = {0x00, 0x01, 0x02, 0x03};
    auto packet = RtpPacket::Build(static_cast<uint8_t>(RtpPayloadType::kAAC),
                                   12345, 67890, 0xABCD1234, true, payload, sizeof(payload));
    uint8_t buffer[1500];
    size_t len = packet.Serialize(buffer, sizeof(buffer));
    auto parsed = RtpPacket::Parse(buffer, len);
    bool ok = (parsed.Sequence() == 12345) && (parsed.Timestamp() == 67890) &&
              (parsed.Ssrc() == 0xABCD1234) && (parsed.PayloadSize() == 4);
    LOG(INFO) << "RTP test: " << (ok ? "PASS" : "FAIL");
}

void TestH264Fragmenter() {
    LOG(INFO) << "=== H.264 Fragmenter Self-Test ===";
    std::vector<uint8_t> nal(2000); nal[0] = 0x65;
    for (size_t i = 1; i < nal.size(); ++i) nal[i] = i & 0xFF;
    auto fragments = H264Fragmenter::Fragment(nal.data(), nal.size(), 1200);
    std::vector<uint8_t> rebuilt;
    bool ok = H264Fragmenter::Defragment(fragments, rebuilt) &&
              rebuilt.size() == nal.size() && rebuilt[0] == 0x65;
    LOG(INFO) << "H.264 test: " << (ok ? "PASS" : "FAIL");
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_minloglevel = google::INFO;

    std::cout << "\n  RTCom - Real-time Communication System\n"
              << "  SIP + RTP/RTCP + FFmpeg (H.264/AAC)\n\n";

    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [-c <config>] [-h]\n";
            return 0;
        } else if (arg == "-c" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    ServerConfig config = config_path.empty() ?
        Config::LoadDefault() : Config::LoadFromFile(config_path);
    Config::PrintConfig(config);

    TestRtpPacket();
    TestH264Fragmenter();

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // ---- SIP UDP socket ----
    UdpSocket sip_sock;
    sip_sock.Create();
    sip_sock.SetNonBlocking(true);
    sip_sock.SetReuseAddr(true);
    if (!sip_sock.Bind(config.sip.local_port)) {
        LOG(ERROR) << "Cannot bind SIP port " << config.sip.local_port;
        return 1;
    }
    LOG(INFO) << "SIP listening on UDP :" << config.sip.local_port;

    // ---- Session Manager (SIP + RTP) ----
    SessionManager session_mgr(config);

    session_mgr.SetSipEventCallback(
        [](const std::string& call_id, SipCallState state) {
            LOG(INFO) << "[CALLBACK] " << call_id << " -> " << SipCallStateToString(state);
        });

    // RTP receive callback: fires when decoded media arrives
    session_mgr.SetRtpPacketCallback(
        [](const std::string& call_id, const uint8_t* data, size_t len, RtpPayloadType pt) {
            LOG(INFO) << "[RTP RECV] call=" << call_id
                      << " pt=" << static_cast<int>(pt)
                      << " size=" << len << " bytes";
        });

    if (!session_mgr.Start()) {
        LOG(ERROR) << "Failed to start session manager";
        return 1;
    }

    session_mgr.GetSipManager().Register();
    LOG(INFO) << "Server ready. Ctrl+C to stop.";

    // ---- Main loop: SIP receive + timer processing ----
    SipManager& sip = session_mgr.GetSipManager();
    char buf[65536];

    while (g_running) {
        // Poll for incoming SIP messages
        std::string src_addr;
        uint16_t src_port;
        ssize_t n = sip_sock.RecvFrom(buf, sizeof(buf) - 1, src_addr, src_port);
        if (n > 0) {
            buf[n] = '\0';
            std::string msg(buf, n);
            LOG(INFO) << "[SIP RECV] " << n << " bytes from " << src_addr << ":" << src_port;

            sip.ProcessIncomingMessage(msg, src_addr, src_port);

            // Auto-respond to INVITE
            if (msg.find("INVITE") != std::string::npos) {
                // Extract call-id (strip @host to match libosip2's osip_call_id_t.number)
                std::string call_id;
                auto cid_pos = msg.find("Call-ID: ");
                if (cid_pos != std::string::npos) {
                    auto cid_end = msg.find('\r', cid_pos);
                    call_id = msg.substr(cid_pos + 9, cid_end - cid_pos - 9);
                    // Strip @host part: "1-153349@127.0.0.1" → "1-153349"
                    auto at_pos = call_id.find('@');
                    if (at_pos != std::string::npos) call_id = call_id.substr(0, at_pos);
                }

                // Extract SDP body and parse remote RTP info
                auto sdp_pos = msg.find("\r\n\r\n");
                if (sdp_pos != std::string::npos && msg.size() > sdp_pos + 4) {
                    std::string sdp_body = msg.substr(sdp_pos + 4);
                    if (!call_id.empty()) sip.ProcessIncomingSdp(call_id, sdp_body);
                }

                // 180 Ringing (echo headers, no SDP)
                std::string ringing = sip.BuildInviteResponse(180, "Ringing");
                sip_sock.SendTo(ringing.c_str(), ringing.size(), src_addr, src_port);

                // 200 OK with SDP answer (echo headers + SDP body)
                std::string sdp_answer = sip.BuildSdpAnswer(call_id);
                std::string ok = sip.BuildInviteResponse(200, "OK", sdp_answer);
                sip_sock.SendTo(ok.c_str(), ok.size(), src_addr, src_port);

                LOG(INFO) << "[SIP SEND] 180 + 200 OK (+SDP) to " << src_addr << ":" << src_port;
            }
            if (msg.find("BYE") != std::string::npos) {
                std::string ok = sip.BuildResponse(200, "OK");
                sip_sock.SendTo(ok.c_str(), ok.size(), src_addr, src_port);
            }
        }

        // Process SIP retransmission timers
        sip.ProcessTimers();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    session_mgr.Stop();
    google::ShutdownGoogleLogging();
    LOG(INFO) << "Server stopped.";
    return 0;
}
