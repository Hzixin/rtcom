// IMPORTANT: glog must be included before any osip2 headers
// because osip2/osip.h may include system headers that define WARNING as a macro
#include <glog/logging.h>
#undef WARNING   // glog defines const int WARNING, but system headers may #define WARNING

#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <stdexcept>

#include "common/types.h"
#include "rtp/rtp_packet.h"
#include "rtp/rtp_session.h"
#include "rtp/rtcp_handler.h"
#include "rtp/jitter_buffer.h"
#include "sip/sip_manager.h"
#include "net/udp_socket.h"
#include "net/thread_pool.h"
#include "net/io_multiplexer.h"
#include "media/media_encoder.h"
#include "media/media_decoder.h"

using namespace rtcom;

static int tests_passed = 0;
static int tests_failed = 0;

// Simple test runner - avoids macro commas issue
void RunTest(const char* name, std::function<void()> test_fn) {
    std::cout << "  " << name << "... ";
    try {
        test_fn();
        std::cout << "PASSED" << std::endl;
        tests_passed++;
    } catch (const std::exception& e) {
        std::cout << "FAIL: " << e.what() << std::endl;
        tests_failed++;
    }
}

#define EXPECT(cond, msg) \
    if (!(cond)) throw std::runtime_error(msg)

// ============================================================================
// RTP Packet Tests
// ============================================================================

void TestRtpPacketBuild() {
    RunTest("Build RTP packet", []() {
        uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
        auto pkt = RtpPacket::Build(96, 100, 1000, 0x12345678, true, payload, 4);
        EXPECT(pkt.Sequence() == 100, "seq mismatch");
        EXPECT(pkt.Timestamp() == 1000, "ts mismatch");
        EXPECT(pkt.Ssrc() == 0x12345678, "ssrc mismatch");
        EXPECT(pkt.Marker(), "marker mismatch");
        EXPECT(pkt.PayloadSize() == 4, "payload size mismatch");
    });
}

void TestRtpPacketSerialize() {
    RunTest("Serialize/Deserialize RTP", []() {
        uint8_t payload[256];
        for (size_t i = 0; i < 256; ++i) payload[i] = static_cast<uint8_t>(i);
        auto pkt = RtpPacket::Build(97, 54321, 99999, 0xDEADBEEF, false, payload, 256);
        uint8_t buf[1500];
        size_t len = pkt.Serialize(buf, sizeof(buf));
        auto parsed = RtpPacket::Parse(buf, len);
        EXPECT(parsed.Sequence() == 54321, "seq");
        EXPECT(parsed.Ssrc() == 0xDEADBEEF, "ssrc");
        EXPECT(parsed.PayloadSize() == 256, "payload");
    });
}

void TestH264Fragmentation() {
    RunTest("H.264 FU-A fragmentation", []() {
        std::vector<uint8_t> nal(3000);
        nal[0] = 0x65;
        for (size_t i = 1; i < nal.size(); ++i) nal[i] = i & 0xFF;
        auto frags = H264Fragmenter::Fragment(nal.data(), nal.size(), 1200);
        EXPECT(frags.size() > 1, "no fragmentation");
        std::vector<uint8_t> rebuilt;
        EXPECT(H264Fragmenter::Defragment(frags, rebuilt), "defragment failed");
        EXPECT(rebuilt.size() == nal.size(), "size mismatch");
        EXPECT(rebuilt[0] == 0x65, "header mismatch");
    });
}

void TestAacPacketization() {
    RunTest("AAC packetization", []() {
        std::vector<uint8_t> aac(512, 0xAA);
        auto rtp = AacPacketizer::Pack(aac.data(), aac.size());
        EXPECT(rtp.size() == aac.size() + 4, "pack size");
        std::vector<uint8_t> unpacked;
        EXPECT(AacPacketizer::Unpack(rtp.data(), rtp.size(), unpacked), "unpack");
        EXPECT(unpacked.size() == aac.size(), "unpack size");
    });
}

// ============================================================================
// RTCP Tests
// ============================================================================

void TestRtcpSenderReport() {
    RunTest("RTCP Sender Report", []() {
        RtcpHandler h(0x12345678);
        auto sr = h.BuildSenderReport(RtcpHandler::NowNtp(), 1000, 100, 20000);
        EXPECT(sr.size() == 28, "size");
        EXPECT(sr[1] == 200, "type");
        std::vector<RtcpReportBlock> blocks;
        uint32_t reporter = 0;
        RtcpHandler::Parse(sr.data(), sr.size(), blocks, reporter);
        EXPECT(reporter == 0x12345678, "reporter ssrc");
    });
}

void TestRtcpCompound() {
    RunTest("RTCP Compound packet", []() {
        RtcpHandler h(0xABCD);
        auto c = h.BuildCompound("test@ex.com", RtcpHandler::NowNtp(), 2000, 500, 100000);
        EXPECT(c.size() > 28, "compound size");
        std::vector<RtcpReportBlock> blocks;
        uint32_t reporter = 0;
        RtcpHandler::Parse(c.data(), c.size(), blocks, reporter);
        EXPECT(reporter == 0xABCD, "reporter");
    });
}

// ============================================================================
// Jitter Buffer Tests
// ============================================================================

void TestJitterBufferBasic() {
    RunTest("Jitter Buffer insert", []() {
        JitterBuffer jb;
        for (int i = 0; i < 10; ++i) {
            jb.Insert(RtpPacket::Build(96, i, i * 160, 0x100, false, nullptr, 0));
        }
        EXPECT(jb.PacketsInserted() == 10, "insert count");
    });
}

void TestJitterBufferLoss() {
    RunTest("Jitter Buffer loss detection", []() {
        JitterBuffer jb;
        jb.Insert(RtpPacket::Build(96, 0, 0, 0x100, false, nullptr, 0));
        jb.Insert(RtpPacket::Build(96, 5, 800, 0x100, false, nullptr, 0));
        EXPECT(jb.PacketsLost() >= 4, "loss detection");
    });
}

// ============================================================================
// Thread Pool Tests
// ============================================================================

void TestThreadPoolBasic() {
    RunTest("Thread Pool", []() {
        ThreadPool pool(4);
        std::atomic<int> c{0};
        for (int i = 0; i < 100; ++i) pool.Submit([&]() { c++; });
        pool.WaitAll();
        EXPECT(c == 100, "counter");
    });
}

// ============================================================================
// SIP Tests
// ============================================================================

void TestSipMessages() {
    RunTest("SIP message building", []() {
        SipConfig cfg;
        cfg.username = "testuser";
        cfg.domain = "test.com";
        cfg.local_addr = "127.0.0.1";
        SipManager mgr;
        EXPECT(mgr.Initialize(cfg), "init");
        auto reg = mgr.BuildRegisterRequest();
        EXPECT(reg.find("REGISTER") != std::string::npos, "method");
        EXPECT(reg.find("testuser") != std::string::npos, "username");
        auto inv = mgr.BuildInviteRequest("call-1", "sip:bob@test.com");
        EXPECT(inv.find("INVITE") != std::string::npos, "invite method");
        EXPECT(inv.find("call-1") != std::string::npos, "call-id");
        mgr.Shutdown();
    });
}

void TestSipCallFlow() {
    RunTest("SIP call flow", []() {
        SipConfig cfg;
        cfg.username = "alice"; cfg.domain = "example.com";
        SipManager mgr;
        mgr.Initialize(cfg);
        std::string cid = mgr.MakeCall("sip:bob@example.com");
        EXPECT(!cid.empty(), "call-id empty");
        EXPECT(mgr.GetCallState(cid) == SipCallState::kCalling, "calling");

        std::string resp =
            "SIP/2.0 200 OK\r\nVia: SIP/2.0/UDP 127.0.0.1:5060;branch=z\r\n"
            "From: <sip:alice@example.com>;tag=a\r\nTo: <sip:bob@example.com>;tag=b\r\n"
            "Call-ID: " + cid + "\r\nCSeq: 1 INVITE\r\nContent-Length: 0\r\n\r\n";
        mgr.ProcessIncomingMessage(resp, "127.0.0.1", 5060);
        EXPECT(mgr.GetCallState(cid) == SipCallState::kConnected, "connected");
        mgr.HangUp(cid);
        mgr.Shutdown();
    });
}

void TestSipIncoming() {
    RunTest("SIP incoming call", []() {
        SipConfig cfg;
        cfg.username = "bob"; cfg.domain = "example.com";
        SipManager mgr;
        mgr.Initialize(cfg);
        std::string inv =
            "INVITE sip:bob@example.com SIP/2.0\r\nVia: SIP/2.0/UDP 10.0.0.1:5060;branch=z\r\n"
            "From: <sip:alice@example.com>;tag=at\r\nTo: <sip:bob@example.com>\r\n"
            "Call-ID: inc-123\r\nCSeq: 1 INVITE\r\n"
            "Contact: <sip:alice@10.0.0.1>\r\nContent-Length: 0\r\n\r\n";
        mgr.ProcessIncomingMessage(inv, "10.0.0.1", 5060);
        EXPECT(mgr.GetCallState("inc-123") == SipCallState::kConnected, "connected");

        std::string bye =
            "BYE sip:bob@example.com SIP/2.0\r\nVia: SIP/2.0/UDP 10.0.0.1:5060;branch=b\r\n"
            "From: <sip:alice@example.com>;tag=at\r\nTo: <sip:bob@example.com>;tag=bt\r\n"
            "Call-ID: inc-123\r\nCSeq: 2 BYE\r\nContent-Length: 0\r\n\r\n";
        mgr.ProcessIncomingMessage(bye, "10.0.0.1", 5060);
        EXPECT(mgr.GetCallState("inc-123") == SipCallState::kIdle, "disconnected");
        mgr.Shutdown();
    });
}

// ============================================================================
// Network Tests
// ============================================================================

void TestUdpSocketCreate() {
    RunTest("UDP socket create/bind", []() {
        UdpSocket s;
        EXPECT(s.Create(), "create");
        EXPECT(s.SetNonBlocking(true), "nonblock");
        EXPECT(s.Bind(0), "bind");
        EXPECT(s.GetLocalPort() > 0, "port");
    });
}

void TestUdpSocketSendRecv() {
    RunTest("UDP socket send/recv", []() {
        UdpSocket snd, rcv;
        snd.Create(); rcv.Create();
        snd.SetNonBlocking(true); rcv.SetNonBlocking(true);
        EXPECT(rcv.Bind(0), "bind");
        uint16_t port = rcv.GetLocalPort();
        const char* msg = "Hello RTP!";
        snd.SendTo(msg, strlen(msg), "127.0.0.1", port);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        char buf[256] = {};
        std::string src; uint16_t sp;
        ssize_t n = rcv.RecvFrom(buf, sizeof(buf), src, sp);
        EXPECT(n == static_cast<ssize_t>(strlen(msg)), "recv size");
    });
}

void TestEpoll() {
    RunTest("epoll basic", []() {
        IoMultiplexer ep(10);
        UdpSocket s; s.Create(); s.SetNonBlocking(true); s.Bind(0);
        bool called = false;
        EXPECT(ep.AddFd(s.GetFd(), EPOLLIN, [&](int, uint32_t) { called = true; }), "add");
        EXPECT(ep.DelFd(s.GetFd()), "del");
    });
}

// ============================================================================
// Media Tests
// ============================================================================

void TestEncoderInit() {
    RunTest("Media encoder init", []() {
        MediaEncoder aac(CodecType::kAudioAAC);
        aac.Initialize(64000, 44100, 1);
        MediaEncoder h264(CodecType::kVideoH264);
        h264.Initialize(1000000, 44100, 1, 640, 480, 30);
    });
}

void TestDecoderInit() {
    RunTest("Media decoder init", []() {
        MediaDecoder aac(CodecType::kAudioAAC);
        EXPECT(aac.Initialize(), "aac init");
        MediaDecoder h264(CodecType::kVideoH264);
        EXPECT(h264.Initialize(), "h264 init");
    });
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_minloglevel = google::GLOG_WARNING;

    std::cout << "\n=== RTCom Test Suite ===\n" << std::endl;

    std::cout << "[RTP]" << std::endl;
    TestRtpPacketBuild(); TestRtpPacketSerialize();
    TestH264Fragmentation(); TestAacPacketization();

    std::cout << "\n[RTCP]" << std::endl;
    TestRtcpSenderReport(); TestRtcpCompound();

    std::cout << "\n[Jitter Buffer]" << std::endl;
    TestJitterBufferBasic(); TestJitterBufferLoss();

    std::cout << "\n[Thread Pool]" << std::endl;
    TestThreadPoolBasic();

    std::cout << "\n[SIP]" << std::endl;
    TestSipMessages(); TestSipCallFlow(); TestSipIncoming();

    std::cout << "\n[Network]" << std::endl;
    TestUdpSocketCreate(); TestUdpSocketSendRecv(); TestEpoll();

    std::cout << "\n[Media]" << std::endl;
    TestEncoderInit(); TestDecoderInit();

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed, "
              << (tests_passed + tests_failed) << " total ===" << std::endl;

    google::ShutdownGoogleLogging();
    return tests_failed > 0 ? 1 : 0;
}
