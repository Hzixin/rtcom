// SIPp integration test: sends SIP messages over UDP to SIPp
#include <glog/logging.h>
#undef WARNING

#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

#include "sip/sip_manager.h"
#include "net/udp_socket.h"

using namespace rtcom;

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_minloglevel = google::INFO;

    std::string target_addr = "127.0.0.1";
    uint16_t target_port = 5060;
    uint16_t local_port = 5070;

    if (argc > 1) target_addr = argv[1];
    if (argc > 2) target_port = static_cast<uint16_t>(std::stoi(argv[2]));

    std::cout << "\n=== SIPp Integration Test ===\n";
    std::cout << "Target: " << target_addr << ":" << target_port << "\n\n";

    SipConfig cfg;
    cfg.username = "testuser";
    cfg.domain = "example.com";
    cfg.local_addr = "127.0.0.1";
    cfg.local_port = local_port;

    SipManager sip_mgr;
    if (!sip_mgr.Initialize(cfg)) {
        std::cerr << "FAIL: init\n";
        return 1;
    }

    UdpSocket sock;
    sock.Create();
    sock.SetNonBlocking(false);
    if (!sock.Bind(local_port)) {
        std::cerr << "FAIL: bind\n";
        return 1;
    }

    int ok = 0, fail = 0;
    char buf[65536];

    // Test 1: REGISTER
    std::cout << "  [1] REGISTER...\n";
    {
        std::string reg = sip_mgr.BuildRegisterRequest();
        std::cout << ">>> " << reg.substr(0, reg.find('\n')) << "...\n";
        sock.SendTo(reg.c_str(), reg.size(), target_addr, target_port);

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::string src; uint16_t sp;
        ssize_t n = sock.RecvFrom(buf, sizeof(buf) - 1, src, sp);
        if (n > 0) {
            buf[n] = '\0';
            std::string resp(buf, n);
            std::cout << "<<< " << resp.substr(0, resp.find('\n')) << "\n";
            if (resp.find("200") != std::string::npos ||
                resp.find("401") != std::string::npos) {
                std::cout << "  REGISTER: PASS\n"; ok++;
            } else { std::cout << "  REGISTER: FAIL\n"; fail++; }
        } else {
            std::cout << "  REGISTER: FAIL (no response)\n"; fail++;
        }
    }

    // Test 2: INVITE
    std::cout << "\n  [2] INVITE...\n";
    {
        std::string cid = sip_mgr.MakeCall("sip:sipp@example.com");
        std::string inv = sip_mgr.BuildInviteRequest(cid, "sip:sipp@example.com");
        std::cout << ">>> " << inv.substr(0, inv.find('\n')) << "...\n";
        sock.SendTo(inv.c_str(), inv.size(), target_addr, target_port);

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::string src; uint16_t sp;
        ssize_t n = sock.RecvFrom(buf, sizeof(buf) - 1, src, sp);
        if (n > 0) {
            buf[n] = '\0';
            std::string resp(buf, n);
            std::cout << "<<< " << resp.substr(0, resp.find('\n')) << "\n";
            sip_mgr.ProcessIncomingMessage(resp, src, sp);
            std::cout << "  Call state: " << SipCallStateToString(sip_mgr.GetCallState(cid)) << "\n";
            std::cout << "  INVITE: PASS\n"; ok++;
        } else {
            std::cout << "  INVITE: FAIL (no response)\n"; fail++;
        }
    }

    sip_mgr.Shutdown();
    std::cout << "\n=== " << ok << "/" << (ok+fail) << " passed ===\n";
    return fail > 0 ? 1 : 0;
}
