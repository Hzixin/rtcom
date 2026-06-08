#include <glog/logging.h>
#undef WARNING  // Prevent macro conflict with system headers via osip2

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include "common/config.h"
#include "sip/sip_manager.h"
#include "rtp/rtp_packet.h"
#include "rtp/rtp_session.h"
#include "session/session_manager.h"

using namespace rtcom;

static std::atomic<bool> g_running{true};

void SignalHandler(int sig) {
    LOG(INFO) << "Received signal " << sig << ", shutting down...";
    g_running = false;
}

void PrintBanner() {
    std::cout << "\n"
              << "  RTCom - Real-time Communication System\n"
              << "  SIP + RTP/RTCP + FFmpeg (H.264/AAC)\n\n";
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
    std::vector<uint8_t> nal(2000);
    nal[0] = 0x65;
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
    PrintBanner();

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

    SessionManager session_mgr(config);
    session_mgr.SetSipEventCallback(
        [](const std::string& call_id, SipCallState state) {
            LOG(INFO) << "[CALLBACK] " << call_id << " -> " << SipCallStateToString(state);
        });

    if (!session_mgr.Start()) {
        LOG(ERROR) << "Failed to start session manager";
        return 1;
    }

    session_mgr.GetSipManager().Register();
    LOG(INFO) << "Server ready. Ctrl+C to stop.";

    while (g_running) {
        session_mgr.GetSipManager().ProcessTimers();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    session_mgr.Stop();
    google::ShutdownGoogleLogging();
    LOG(INFO) << "Server stopped.";
    return 0;
}
