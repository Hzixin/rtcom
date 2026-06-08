// Simple SIP server: listens on UDP, uses SipManager to process messages
#include <glog/logging.h>
#undef WARNING

#include <iostream>
#include <csignal>
#include <atomic>
#include <cstring>
#include <thread>
#include <chrono>

#include "sip/sip_manager.h"
#include "net/udp_socket.h"

using namespace rtcom;

static std::atomic<bool> g_running{true};
void SignalHandler(int) { g_running = false; }

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_minloglevel = google::INFO;

    uint16_t port = 5070;

    SipConfig cfg;
    cfg.username = "bob";
    cfg.domain = "example.com";
    cfg.local_addr = "127.0.0.1";
    cfg.local_port = port;

    SipManager sip_mgr;
    sip_mgr.Initialize(cfg);
    sip_mgr.SetSipEventCallback(
        [](const std::string& cid, SipCallState st) {
            std::cout << "[SIP] " << cid << " -> " << SipCallStateToString(st) << "\n";
        });

    UdpSocket sock;
    sock.Create();
    sock.SetNonBlocking(true);
    sock.SetReuseAddr(true);
    sock.Bind(port);

    std::cout << "=== SIP Server on UDP :" << port << " ===\n";
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    char buf[65536];
    while (g_running) {
        std::string src; uint16_t sp;
        ssize_t n = sock.RecvFrom(buf, sizeof(buf) - 1, src, sp);
        if (n > 0) {
            buf[n] = '\0';
            std::string msg(buf, n);
            std::cout << ">>> " << msg.substr(0, msg.find('\n')) << "\n";

            sip_mgr.ProcessIncomingMessage(msg, src, sp);
            sip_mgr.ProcessTimers();

            if (msg.find("INVITE") != std::string::npos) {
                std::string r = sip_mgr.BuildResponse(180, "Ringing");
                sock.SendTo(r.c_str(), r.size(), src, sp);
                std::string ok = sip_mgr.BuildResponse(200, "OK");
                sock.SendTo(ok.c_str(), ok.size(), src, sp);
                std::cout << "<<< 180 + 200\n";
            }
            if (msg.find("BYE") != std::string::npos) {
                std::string ok = sip_mgr.BuildResponse(200, "OK");
                sock.SendTo(ok.c_str(), ok.size(), src, sp);
                std::cout << "<<< 200\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    sip_mgr.Shutdown();
    return 0;
}
