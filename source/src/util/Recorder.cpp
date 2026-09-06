#include "util/Recorder.h"
#include <algorithm>
#include <sstream>
#include <thread>

Recorder::~Recorder() { Stop(); }

bool Recorder::Start(const std::string& outputPath, int width, int height, int fps, const char* pixelFormat) {
    Stop();

    // The sim's own thread pool (ParallelFor) already saturates every hardware thread every
    // substep. x264 defaults to using just as many threads for real-time encoding, so left
    // unbounded it would compete 1:1 with the simulation for CPU time and slow it down just as
    // badly as a blocking pipe would have. Capping it to a quarter of the machine (and using the
    // cheapest preset) keeps encoding cheap enough to not be felt by the sim.
    const unsigned encoderThreads = std::max(1u, std::thread::hardware_concurrency() / 4);

    std::ostringstream cmd;
    cmd << "ffmpeg -y -loglevel error"
        << " -f rawvideo -pixel_format " << pixelFormat
        << " -video_size " << width << 'x' << height
        << " -framerate " << fps
        << " -i - -c:v libx264 -threads " << encoderThreads << " -preset ultrafast -crf 20 -pix_fmt yuv420p"
        << " \"" << outputPath << "\"";

#ifdef _WIN32
    pipe_ = _popen(cmd.str().c_str(), "wb");
#else
    pipe_ = popen(cmd.str().c_str(), "w");
#endif
    if (!pipe_) {
        fprintf(stderr, "Recorder: failed to launch ffmpeg (is it on PATH?)\n");
        return false;
    }

    stopping_ = false;
    worker_   = std::thread(&Recorder::WriterLoop, this);
    return true;
}

void Recorder::SubmitFrame(const void* data, size_t bytes, std::function<void()> release) {
    if (!pipe_) { release(); return; }

    std::lock_guard<std::mutex> lock(queueMutex_);
    if (queue_.size() >= kMaxQueuedFrames) { release(); return; } // encoder can't keep up — drop rather than stall the caller

    queue_.push_back({ data, bytes, std::move(release) });
    queueCv_.notify_one();
}

void Recorder::WriterLoop() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [&] { return !queue_.empty() || stopping_; });
            if (queue_.empty() && stopping_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        // The (potentially slow, cross-PCIe) read of `job.data` happens right here, off the
        // caller's thread — that's the whole point of this queue.
        fwrite(job.data, 1, job.bytes, pipe_);
        job.release();
    }
}

void Recorder::Stop() {
    if (!pipe_) return;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopping_ = true;
    }
    queueCv_.notify_all();
    if (worker_.joinable()) worker_.join(); // drains whatever is still queued before we close the pipe

#ifdef _WIN32
    _pclose(pipe_);
#else
    pclose(pipe_);
#endif
    pipe_ = nullptr;
    queue_.clear();
}
