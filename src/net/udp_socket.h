#pragma once

#include <string>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>

namespace rtcom {

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    // Non-copyable but movable
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    bool Create();
    bool Bind(uint16_t port, const std::string& addr = "0.0.0.0");
    void Close();

    ssize_t SendTo(const void* data, size_t len,
                   const std::string& dest_addr, uint16_t dest_port);
    ssize_t RecvFrom(void* buf, size_t buf_len,
                     std::string& src_addr, uint16_t& src_port);

    bool SetNonBlocking(bool nonblock);
    bool SetReuseAddr(bool reuse);
    bool SetRecvBuffer(int size);
    bool SetSendBuffer(int size);

    int  GetFd() const { return fd_; }
    bool IsValid() const { return fd_ >= 0; }
    uint16_t GetLocalPort() const;

private:
    int fd_;
    struct sockaddr_in local_addr_;
};

} // namespace rtcom
