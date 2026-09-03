#include "util/ThreadPool.h"

#include <algorithm>
#include <condition_variable>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

namespace {

class ThreadPool {
public:
    ThreadPool() {
        const unsigned n = std::max(1u, std::thread::hardware_concurrency());
        threads_.reserve(n);
        for (unsigned i = 0; i < n; i++)
            threads_.emplace_back([this, i] { WorkerLoop(i); });
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            generation_++;
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }

    void Dispatch(int32_t total, const std::function<void(int32_t, int32_t)>& fn) {
        const int32_t chunks = std::min<int32_t>(static_cast<int32_t>(threads_.size()), total);
        if (chunks <= 0) return;

        std::latch done(chunks);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_       = &fn;
            jobTotal_  = total;
            jobChunks_ = chunks;
            jobDone_   = &done;
            generation_++;
        }
        cv_.notify_all();
        done.wait();
    }

private:
    void WorkerLoop(unsigned id) {
        uint64_t seenGeneration = 0;
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return generation_ != seenGeneration; });
            seenGeneration = generation_;
            if (stopping_) return;

            const auto*  job    = job_;
            const int32_t total  = jobTotal_;
            const int32_t chunks = jobChunks_;
            std::latch*  done   = jobDone_;
            lock.unlock();

            if (static_cast<int32_t>(id) < chunks) {
                const int32_t begin = static_cast<int32_t>(static_cast<int64_t>(total) * id / chunks);
                const int32_t end   = static_cast<int32_t>(static_cast<int64_t>(total) * (id + 1) / chunks);
                (*job)(begin, end);
                done->count_down();
            }
        }
    }

    std::vector<std::thread> threads_;
    std::mutex                mutex_;
    std::condition_variable   cv_;
    uint64_t                  generation_ = 0;
    bool                      stopping_   = false;

    // Set under mutex_ each Dispatch() call; workers copy these out before releasing the lock.
    const std::function<void(int32_t, int32_t)>* job_ = nullptr;
    int32_t    jobTotal_  = 0;
    int32_t    jobChunks_ = 0;
    std::latch* jobDone_  = nullptr;
};

} // namespace

void ParallelFor(int32_t total, const std::function<void(int32_t, int32_t)>& fn) {
    static ThreadPool pool;
    pool.Dispatch(total, fn);
}
