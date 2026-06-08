#include "io_multiplexer.h"
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <glog/logging.h>

namespace rtcom {

IoMultiplexer::IoMultiplexer(int max_events) : max_events_(max_events) {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        LOG(FATAL) << "Failed to create epoll fd: " << strerror(errno);
    }
    LOG(INFO) << "IO Multiplexer (epoll) ready, max_events=" << max_events;
}

IoMultiplexer::~IoMultiplexer() {
    Stop();
    if (epoll_fd_ >= 0) close(epoll_fd_);
}

bool IoMultiplexer::AddFd(int fd, uint32_t events, EventCallback callback) {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        LOG(ERROR) << "epoll_ctl ADD fd=" << fd << ": " << strerror(errno);
        return false;
    }
    fd_map_[fd] = {std::move(callback), events};
    return true;
}

bool IoMultiplexer::ModFd(int fd, uint32_t events) {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        LOG(ERROR) << "epoll_ctl MOD fd=" << fd << ": " << strerror(errno);
        return false;
    }
    auto it = fd_map_.find(fd);
    if (it != fd_map_.end()) it->second.events = events;
    return true;
}

bool IoMultiplexer::DelFd(int fd) {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        LOG(ERROR) << "epoll_ctl DEL fd=" << fd << ": " << strerror(errno);
        return false;
    }
    fd_map_.erase(fd);
    return true;
}

uint64_t IoMultiplexer::GetNowMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int IoMultiplexer::GetNextTimeout() const {
    int timeout = -1;
    uint64_t now = GetNowMs();
    for (const auto& timer : timers_) {
        int64_t remaining = static_cast<int64_t>(timer.next_fire_ms) - now;
        if (remaining <= 0) return 0;
        if (timeout < 0 || remaining < timeout) {
            timeout = static_cast<int>(remaining);
        }
    }
    return timeout;
}

void IoMultiplexer::FireTimers() {
    uint64_t now = GetNowMs();
    std::lock_guard<std::mutex> lock(timer_mutex_);
    for (auto& timer : timers_) {
        if (now >= timer.next_fire_ms) {
            timer.callback();
            timer.next_fire_ms = timer.recurring ? now + timer.interval_ms : UINT64_MAX;
        }
    }
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [](const TimerInfo& t) {
                           return !t.recurring && t.next_fire_ms == UINT64_MAX;
                       }),
        timers_.end());
}

void IoMultiplexer::Run(int timeout_ms) {
    running_ = true;
    LOG(INFO) << "Epoll event loop started";
    std::vector<struct epoll_event> events(max_events_);

    while (running_) {
        int actual_timeout = timeout_ms >= 0 ? timeout_ms : GetNextTimeout();
        int nfds = epoll_wait(epoll_fd_, events.data(), max_events_, actual_timeout);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG(ERROR) << "epoll_wait failed: " << strerror(errno);
            break;
        }

        FireTimers();

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;
            std::lock_guard<std::mutex> lock(fd_mutex_);
            auto it = fd_map_.find(fd);
            if (it != fd_map_.end() && it->second.callback) {
                it->second.callback(fd, revents);
            }
        }
    }
    LOG(INFO) << "Epoll event loop stopped";
}

void IoMultiplexer::Stop() {
    running_ = false;
}

uint64_t IoMultiplexer::AddTimer(uint64_t interval_ms, bool recurring, TimerCallback callback) {
    std::lock_guard<std::mutex> lock(timer_mutex_);
    uint64_t id = next_timer_id_++;
    timers_.push_back({id, interval_ms, recurring,
                       std::move(callback), GetNowMs() + interval_ms});
    return id;
}

void IoMultiplexer::RemoveTimer(uint64_t timer_id) {
    std::lock_guard<std::mutex> lock(timer_mutex_);
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [timer_id](const TimerInfo& t) { return t.id == timer_id; }),
        timers_.end());
}

} // namespace rtcom
