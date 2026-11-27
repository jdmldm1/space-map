#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency()) : stop_(false), in_flight_(0) {
        const size_t count = std::max(size_t(1), std::min(threads, size_t(8)));
        for (size_t i = 0; i < count; ++i) {
            workers_.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex_);
                        this->cv_.wait(lock, [this]() { return this->stop_ || !this->tasks_.empty(); });
                        if (this->stop_ && this->tasks_.empty()) return;
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    task();
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex_);
                        --in_flight_;
                        if (in_flight_ == 0) {
                            done_cv_.notify_all();
                        }
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    void parallel_for(size_t total, size_t num_chunks, const std::function<void(size_t, size_t)>& fn) {
        if (workers_.size() <= 1 || total <= 2000 || num_chunks <= 1) {
            fn(0, total);
            return;
        }
        const size_t chunks = std::min(num_chunks, workers_.size());
        const size_t chunk_size = (total + chunks - 1) / chunks;
        size_t queued = 0;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            for (size_t k = 1; k < chunks; ++k) {
                const size_t start = k * chunk_size;
                const size_t end = std::min(total, (k + 1) * chunk_size);
                if (start < total) {
                    tasks_.emplace([fn, start, end]() { fn(start, end); });
                    ++queued;
                    ++in_flight_;
                }
            }
            if (queued > 0) {
                cv_.notify_all();
            }
        }

        // Run chunk 0 on the caller's thread
        fn(0, std::min(total, chunk_size));

        // Wait for worker chunks to finish
        if (queued > 0) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            done_cv_.wait(lock, [this]() { return in_flight_ == 0; });
        }
    }

    size_t size() const { return workers_.size(); }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::condition_variable done_cv_;
    bool stop_;
    size_t in_flight_;
};
