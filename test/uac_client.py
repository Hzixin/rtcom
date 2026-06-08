#!/usr/bin/env python3
"""
SIP UAC (User Agent Client) + RTP Sender with real audio file
  INVITE -> 180 -> 200 OK (with SDP) -> ACK -> RTP audio -> BYE -> 200 OK
Reads a WAV file and sends it as RTP PCMA (A-law).
Usage: python3 uac_client.py [wav_file]
"""
import socket
import struct
import time
import random
import os
import sys
import re
import math
import audioop
import wave

# ---- Configuration ----
SERVER_IP = "127.0.0.1"
SERVER_PORT = 5060
LOCAL_IP = "127.0.0.1"
LOCAL_SIP_PORT = 5091
LOCAL_RTP_PORT = 6010

# ---- Audio source ----
WAV_FILE = sys.argv[1] if len(sys.argv) > 1 else "/tmp/test_pcm.wav"
MAX_SECONDS = 30  # Only send first N seconds to keep test fast

def rand_hex(n):
    return ''.join(random.choice('0123456789abcdef') for _ in range(n))

call_id = f"{rand_hex(8)}-{rand_hex(6)}@{LOCAL_IP}"
local_tag = rand_hex(8)
branch = f"z9hG4bK{rand_hex(32)}"
cseq_invite = 1
cseq_bye = 2
peer_tag = "unknown"

# ---- Sockets ----
sip_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sip_sock.bind((LOCAL_IP, LOCAL_SIP_PORT))
sip_sock.settimeout(5.0)

rtp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
rtp_sock.bind((LOCAL_IP, LOCAL_RTP_PORT))

# ---- Read WAV file, encode to A-law using audioop ----
print(f"Loading WAV: {WAV_FILE}")
with wave.open(WAV_FILE, 'rb') as wf:
    wf_sample_rate = wf.getframerate()
    wf_channels = wf.getnchannels()
    wf_sampwidth = wf.getsampwidth()
    wf_frames = wf.getnframes()
    wf_data = wf.readframes(wf_frames)

print(f"  {wf_sample_rate}Hz, {wf_channels}ch, {wf_sampwidth*8}bit, {wf_frames} frames ({wf_frames/wf_sample_rate:.1f}s)")

# Convert to mono 16-bit PCM if needed
if wf_channels == 2:
    wf_data = audioop.tomono(wf_data, wf_sampwidth, 1, 0)
if wf_sampwidth != 2:
    wf_data = audioop.lin2lin(wf_data, wf_sampwidth, 2)

# Encode to A-law in 160-sample (20ms) chunks
print("Encoding to A-law (audioop.lin2alaw)...")
FRAME_SAMPLES = 160  # 20ms at 8kHz
alaw_frames = []
for i in range(0, len(wf_data) - FRAME_SAMPLES * 2, FRAME_SAMPLES * 2):
    chunk = wf_data[i:i + FRAME_SAMPLES * 2]
    if len(chunk) < FRAME_SAMPLES * 2:
        break
    alaw_frames.append(audioop.lin2alaw(chunk, 2))

total_duration = len(alaw_frames) * 0.020
print(f"  {len(alaw_frames)} A-law frames ({total_duration:.1f}s)")

# RTP params
SAMPLE_RATE = 8000
FRAME_SAMPLES = 160  # 20ms
SSRC = random.randint(0, 0xFFFFFFFF)
seq = random.randint(0, 65535)
ts = random.randint(0, 0xFFFFFFFF)

def make_rtp_header(seq, ts, ssrc, pt, marker=0):
    return bytes([
        0x80,           # V=2, P=0, X=0, CC=0
        (pt & 0x7F),    # M=0, PT=payload type
        (seq >> 8) & 0xFF,
        seq & 0xFF,
        (ts >> 24) & 0xFF,
        (ts >> 16) & 0xFF,
        (ts >> 8) & 0xFF,
        ts & 0xFF,
        (ssrc >> 24) & 0xFF,
        (ssrc >> 16) & 0xFF,
        (ssrc >> 8) & 0xFF,
        ssrc & 0xFF,
    ])

# ============================================================
# Step 1: INVITE with SDP
# ============================================================
sdp_offer = (
    f"v=0\r\n"
    f"o=sipp 1 1 IN IP4 {LOCAL_IP}\r\n"
    f"s=-\r\n"
    f"c=IN IP4 {LOCAL_IP}\r\n"
    f"t=0 0\r\n"
    f"m=audio {LOCAL_RTP_PORT} RTP/AVP 8\r\n"
    f"a=rtpmap:8 PCMA/8000\r\n"
)

invite = (
    f"INVITE sip:rtcom@{SERVER_IP}:{SERVER_PORT} SIP/2.0\r\n"
    f"Via: SIP/2.0/UDP {LOCAL_IP}:{LOCAL_SIP_PORT};branch={branch}\r\n"
    f"From: <sip:sipp@{LOCAL_IP}:{LOCAL_SIP_PORT}>;tag={local_tag}\r\n"
    f"To: <sip:rtcom@{SERVER_IP}:{SERVER_PORT}>\r\n"
    f"Call-ID: {call_id}\r\n"
    f"CSeq: {cseq_invite} INVITE\r\n"
    f"Contact: <sip:sipp@{LOCAL_IP}:{LOCAL_SIP_PORT}>\r\n"
    f"Max-Forwards: 70\r\n"
    f"Content-Type: application/sdp\r\n"
    f"Content-Length: {len(sdp_offer)}\r\n"
    f"\r\n"
    f"{sdp_offer}"
)

print("=" * 60)
print("SIP UAC + RTP Client - Real Audio Test")
print("=" * 60)
print(f"\n>>> Step 1: INVITE -> {SERVER_IP}:{SERVER_PORT}")
sip_sock.sendto(invite.encode(), (SERVER_IP, SERVER_PORT))
print("    INVITE sent")

# ============================================================
# Step 2-3: 180 + 200 OK
# ============================================================
resp_180 = False
for _ in range(3):
    try:
        data, addr = sip_sock.recvfrom(65536)
        resp = data.decode()
        first_line = resp.split('\r\n')[0]
        if '180' in first_line:
            print(f"\n<<< Step 2: {first_line} (Ringing)")
            resp_180 = True
        elif '200' in first_line:
            if not resp_180:
                print(f"\n<<< Step 2-3: {first_line}")
            else:
                print(f"\n<<< Step 3: {first_line}")
            m = re.search(r'm=audio (\d+)', resp)
            remote_rtp_port = int(m.group(1)) if m else None
            m2 = re.search(r'To:.*?;tag=([^\r\n]+)', resp)
            if m2: peer_tag = m2.group(1)
            print(f"    RTP port={remote_rtp_port}, To-tag={peer_tag}")
            break
    except socket.timeout:
        print("    timeout")
        break

if not remote_rtp_port:
    print("FAIL: no RTP port")
    sys.exit(1)

# ============================================================
# Step 4: ACK
# ============================================================
ack = (
    f"ACK sip:rtcom@{SERVER_IP}:{SERVER_PORT} SIP/2.0\r\n"
    f"Via: SIP/2.0/UDP {LOCAL_IP}:{LOCAL_SIP_PORT};branch={branch}\r\n"
    f"From: <sip:sipp@{LOCAL_IP}:{LOCAL_SIP_PORT}>;tag={local_tag}\r\n"
    f"To: <sip:rtcom@{SERVER_IP}:{SERVER_PORT}>;tag={peer_tag}\r\n"
    f"Call-ID: {call_id}\r\n"
    f"CSeq: {cseq_invite} ACK\r\n"
    f"Contact: <sip:sipp@{LOCAL_IP}:{LOCAL_SIP_PORT}>\r\n"
    f"Max-Forwards: 70\r\n"
    f"Content-Length: 0\r\n"
    f"\r\n"
)
print(f"\n>>> Step 4: ACK")
sip_sock.sendto(ack.encode(), (SERVER_IP, SERVER_PORT))
print(f"    Call established!")

# ============================================================
# Step 5: RTP Audio
# ============================================================
play_frames = min(len(alaw_frames), int(MAX_SECONDS / 0.020))
print(f"\n>>> Step 5: RTP Audio ({play_frames} frames, {play_frames*0.02:.1f}s)")
print(f"    Target: {SERVER_IP}:{remote_rtp_port}")

frames_sent = 0
for frame_idx in range(play_frames):
    payload = alaw_frames[frame_idx]
    pkt = make_rtp_header(seq, ts, SSRC, pt=8) + payload
    rtp_sock.sendto(pkt, (SERVER_IP, remote_rtp_port))
    seq = (seq + 1) & 0xFFFF
    ts += FRAME_SAMPLES
    frames_sent += 1
    time.sleep(0.020)

print(f"    Done: {frames_sent} frames sent")

time.sleep(0.5)

# ============================================================
# Step 6: BYE
# ============================================================
bye = (
    f"BYE sip:rtcom@{SERVER_IP}:{SERVER_PORT} SIP/2.0\r\n"
    f"Via: SIP/2.0/UDP {LOCAL_IP}:{LOCAL_SIP_PORT};branch={branch}\r\n"
    f"From: <sip:sipp@{LOCAL_IP}:{LOCAL_SIP_PORT}>;tag={local_tag}\r\n"
    f"To: <sip:rtcom@{SERVER_IP}:{SERVER_PORT}>;tag={peer_tag}\r\n"
    f"Call-ID: {call_id}\r\n"
    f"CSeq: {cseq_bye} BYE\r\n"
    f"Contact: <sip:sipp@{LOCAL_IP}:{LOCAL_SIP_PORT}>\r\n"
    f"Max-Forwards: 70\r\n"
    f"Content-Length: 0\r\n"
    f"\r\n"
)
print(f"\n>>> Step 6: BYE")
sip_sock.sendto(bye.encode(), (SERVER_IP, SERVER_PORT))

try:
    data, addr = sip_sock.recvfrom(65536)
    first_line = data.decode().split('\r\n')[0]
    print(f"<<< Step 7: {first_line}")
except socket.timeout:
    print(f"    timeout")

print(f"\n{'=' * 60}")
print(f"CALL COMPLETE")
print(f"Call-ID: {call_id}")
print(f"Audio sent: {frames_sent} A-law frames ({frames_sent*0.02:.1f}s)")
print(f"{'=' * 60}")

sip_sock.close()
rtp_sock.close()
