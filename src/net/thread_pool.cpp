#include "thread_pool.h"
#include <glog/logging.h>

namespace rtcom {

ThreadPool::ThreadPool(size_t num_threads) {
    LOG(INFO) << "Thread pool: " << num_threads << " workers";
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::WorkerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    stop_ = true;
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    LOG(INFO) << "Thread pool destroyed";
}

void ThreadPool::Submit(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void ThreadPool::WaitAll() {
    std::unique_lock<std::mutex> lock(mutex_);
    done_cv_.wait(lock, [this] {
        return tasks_.empty() && active_tasks_ == 0;
    });
}

void ThreadPool::WorkerLoop() {
    while (!stop_) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
            ++active_tasks_;
        }

        try {
            task();
        } catch (const std::exception& e) {
            LOG(ERROR) << "Thread pool exception: " << e.what();
        } catch (...) {
            LOG(ERROR) << "Thread pool unknown exception";
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_tasks_;
        }
        done_cv_.notify_all();
    }
}

} // namespace rtcom
