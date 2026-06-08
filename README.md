# RTCom - Real-time Audio/Video Communication System

基于 C++ 实现的实时音视频通信系统，支持 SIP 信令（oSIP）、RTP/RTCP 媒体传输、FFmpeg 编解码（H.264/AAC），支持注册、呼叫、应答、挂断、超时重传及多方会话管理。

## 项目结构

```
video/
├── CMakeLists.txt            # 构建配置
├── README.md                 # 本文件
├── src/
│   ├── common/               # 公共类型与配置
│   │   ├── types.h           # SIP/RTP/Media 类型定义
│   │   ├── config.h/cpp      # 配置加载/保存
│   ├── sip/                  # SIP 信令模块 (libosip2)
│   │   ├── sip_call.h/cpp    # 单个呼叫状态机
│   │   └── sip_manager.h/cpp # SIP 注册/呼叫/响应管理
│   ├── media/                # 媒体处理模块 (FFmpeg)
│   │   ├── media_capture.h/cpp  # 音视频采集/解封装
│   │   ├── media_encoder.h/cpp  # H.264/AAC 编码
│   │   └── media_decoder.h/cpp  # H.264/AAC 解码
│   ├── rtp/                  # RTP/RTCP 传输层
│   │   ├── rtp_packet.h/cpp  # RTP 包构建/解析 (RFC 3550)
│   │   ├── rtp_session.h/cpp # RTP 会话管理
│   │   ├── rtcp_handler.h/cpp # RTCP SR/RR/SDES
│   │   └── jitter_buffer.h/cpp # 自适应抖动缓冲区
│   ├── session/              # 会话管理
│   │   └── session_manager.h/cpp # SIP+RTP+Media 编排
│   ├── net/                  # 网络层
│   │   ├── udp_socket.h/cpp  # UDP Socket 封装
│   │   ├── io_multiplexer.h/cpp # epoll 多路复用
│   │   └── thread_pool.h/cpp # 线程池
│   └── main.cpp              # 服务入口
└── test/
    └── test_main.cpp         # 17 项单元测试
```

## 功能特性

| 模块 | 功能 |
|------|------|
| **SIP 信令** | REGISTER 注册、INVITE 呼叫、ACK 确认、BYE 挂断、CANCEL 取消、超时重传 |
| **RTP 传输** | RTP 包构建/解析、H.264 FU-A 分片、AAC 打包、序列号跟踪 |
| **RTCP 控制** | SR (发送报告)、RR (接收报告)、SDES (CNAME)、NTP 时间戳 |
| **抖动缓冲** | 自适应延迟调整、丢包检测、乱序重排 |
| **媒体编解码** | H.264 视频编码/解码、AAC 音频编码/解码、YUV/PCM 格式转换 |
| **网络层** | UDP Socket、epoll IO 多路复用、线程池 |
| **会话管理** | 多方呼叫管理、SIP+RTP 编排、媒体流转发 |

## 依赖

| 依赖 | Ubuntu 包 | 用途 |
|------|-----------|------|
| libosip2 (>=4.x) | `libosip2-dev` | SIP 协议解析 |
| FFmpeg (libavcodec/libavformat/libavutil/libswscale) | `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev` | 音视频编解码 |
| Google Logging | `libgoogle-glog-dev` | 日志记录 |
| CMake (>=3.14) | `cmake` | 构建系统 |
| g++ (>=11) | `g++` | C++17 编译器 |

### 安装依赖 (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y g++ cmake pkg-config \
    libosip2-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libgoogle-glog-dev
```

## 构建

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## 运行

### 启动服务端

```bash
./build/rtcom_server
```

### 运行测试

```bash
./build/rtcom_test
```

## 关键技术点

1. **SIP over UDP**: 基于 libosip2 实现完整的 SIP 信令交互流程，支持 T1/T2/T4 定时器和指数退避重传
2. **RTP/RTCP (RFC 3550)**: 自定义 RTP 协议栈，支持 H.264 FU-A 分片 (RFC 6184) 和 AAC 打包 (RFC 3640)
3. **自适应抖动缓冲**: 根据网络抖动动态调整缓冲深度 (20-200ms)，丢包检测与乱序重排
4. **epoll + 线程池**: 使用 epoll 进行高效 IO 多路复用，线程池处理编解码等 CPU 密集型任务
5. **FFmpeg 编解码**: 集成 libavcodec 实现 H.264 视频和 AAC 音频的编解码与格式转换
