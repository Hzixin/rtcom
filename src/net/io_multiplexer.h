#pragma once

#include <sys/epoll.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>

namespace rtcom {

class IoMultiplexer {
public:
    using EventCallback = std::function<void(int fd, uint32_t events)>;
    using TimerCallback = std::function<void()>;

    explicit IoMultiplexer(int max_events = 1024);
    ~IoMultiplexer();

    IoMultiplexer(const IoMultiplexer&) = delete;
    IoMultiplexer& operator=(const IoMultiplexer&) = delete;

    bool AddFd(int fd, uint32_t events, EventCallback callback);
    bool ModFd(int fd, uint32_t events);
    bool DelFd(int fd);

    void Run(int timeout_ms = -1);
    void Stop();

    uint64_t AddTimer(uint64_t interval_ms, bool recurring, TimerCallback callback);
    void RemoveTimer(uint64_t timer_id);

private:
    int epoll_fd_;
    int max_events_;
    std::atomic<bool> running_{false};

    struct FdInfo {
        EventCallback callback;
        uint32_t events;
    };
    std::unordered_map<int, FdInfo> fd_map_;
    std::mutex fd_mutex_;

    struct TimerInfo {
        uint64_t id;
        uint64_t interval_ms;
        bool recurring;
        TimerCallback callback;
        uint64_t next_fire_ms;
    };
    std::vector<TimerInfo> timers_;
    std::mutex timer_mutex_;
    uint64_t next_timer_id_{1};

    uint64_t GetNowMs() const;
    int  GetNextTimeout() const;
    void FireTimers();
};

} // namespace rtcom
