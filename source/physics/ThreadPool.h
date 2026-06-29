#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(int n) {
        for (int i = 0; i < n; i++)
            workers_.emplace_back([this] { Loop(); });
    }
    ~ThreadPool() {
        { std::lock_guard lk(mu_); stop_ = true; }
        work_.notify_all();
        for (auto& w : workers_) w.join();
    }
    void Submit(std::function<void()> f) {
        { std::lock_guard lk(mu_); tasks_.push(std::move(f)); ++pending_; }
        work_.notify_one();
    }
    void Wait() {
        std::unique_lock lk(mu_);
        idle_.wait(lk, [this] { return pending_ == 0; });
    }

private:
    void Loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lk(mu_);
                work_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
            {
                std::lock_guard lk(mu_);
                if (--pending_ == 0) idle_.notify_all();
            }
        }
    }

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mu_;
    std::condition_variable           work_, idle_;
    int                               pending_{ 0 };
    bool                              stop_{ false };
};
