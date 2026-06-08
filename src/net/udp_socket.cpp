#include "udp_socket.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <arpa/inet.h>
#include <glog/logging.h>

namespace rtcom {

UdpSocket::UdpSocket() : fd_(-1) {
    memset(&local_addr_, 0, sizeof(local_addr_));
}

UdpSocket::~UdpSocket() { Close(); }

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_) {
    memcpy(&local_addr_, &other.local_addr_, sizeof(local_addr_));
    other.fd_ = -1;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        Close();
        fd_ = other.fd_;
        memcpy(&local_addr_, &other.local_addr_, sizeof(local_addr_));
        other.fd_ = -1;
    }
    return *this;
}

bool UdpSocket::Create() {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        LOG(ERROR) << "Failed to create UDP socket: " << strerror(errno);
        return false;
    }
    return true;
}

bool UdpSocket::Bind(uint16_t port, const std::string& addr) {
    memset(&local_addr_, 0, sizeof(local_addr_));
    local_addr_.sin_family = AF_INET;
    local_addr_.sin_port = htons(port);
    local_addr_.sin_addr.s_addr = (addr == "0.0.0.0") ? INADDR_ANY : inet_addr(addr.c_str());

    if (bind(fd_, (struct sockaddr*)&local_addr_, sizeof(local_addr_)) < 0) {
        LOG(ERROR) << "Bind failed to " << addr << ":" << port << " - " << strerror(errno);
        return false;
    }
    LOG(INFO) << "UDP socket bound to " << addr << ":" << port;
    return true;
}

void UdpSocket::Close() {
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
}

ssize_t UdpSocket::SendTo(const void* data, size_t len,
                          const std::string& dest_addr, uint16_t dest_port) {
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(dest_port);
    dest.sin_addr.s_addr = inet_addr(dest_addr.c_str());

    ssize_t sent = sendto(fd_, data, len, 0, (struct sockaddr*)&dest, sizeof(dest));
    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG(WARNING) << "UDP sendto failed: " << strerror(errno);
        }
    }
    return sent;
}

ssize_t UdpSocket::RecvFrom(void* buf, size_t buf_len,
                            std::string& src_addr, uint16_t& src_port) {
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);
    memset(&src, 0, sizeof(src));

    ssize_t received = recvfrom(fd_, buf, buf_len, 0, (struct sockaddr*)&src, &src_len);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        LOG(WARNING) << "UDP recvfrom failed: " << strerror(errno);
        return -1;
    }

    char addr_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src.sin_addr, addr_buf, sizeof(addr_buf));
    src_addr = addr_buf;
    src_port = ntohs(src.sin_port);
    return received;
}

bool UdpSocket::SetNonBlocking(bool nonblock) {
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) return false;
    flags = nonblock ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(fd_, F_SETFL, flags) >= 0;
}

bool UdpSocket::SetReuseAddr(bool reuse) {
    int opt = reuse ? 1 : 0;
    return setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) >= 0;
}

bool UdpSocket::SetRecvBuffer(int size) {
    return setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) >= 0;
}

bool UdpSocket::SetSendBuffer(int size) {
    return setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) >= 0;
}

uint16_t UdpSocket::GetLocalPort() const {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getsockname(fd_, (struct sockaddr*)&addr, &addr_len) == 0) {
        return ntohs(addr.sin_port);
    }
    return 0;
}

} // namespace rtcom
