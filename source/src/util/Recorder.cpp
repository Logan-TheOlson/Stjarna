#include "util/Recorder.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>
#ifndef _WIN32
#include <csignal>
#endif

Recorder::~Recorder() { Stop(); }

bool Recorder::Start(const std::string& outputPath, int width, int height, int fps, const char* pixelFormat) {
    Stop();

#ifndef _WIN32
    // Writing to the ffmpeg pipe after it exits (crashed, killed, or never execed because ffmpeg
    // isn't on PATH) raises SIGPIPE, whose default disposition kills this whole process. Ignoring
    // it makes the fwrite in WriterLoop fail with EPIPE instead, which the existing "drop rather
    // than stall" queuing already tolerates.
    signal(SIGPIPE, SIG_IGN);
#endif

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

    stopping_   = false;
    workerDone_ = false;
    worker_     = std::thread(&Recorder::WriterLoop, this);
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
            if (queue_.empty() && stopping_) break;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        // The (potentially slow, cross-PCIe) read of `job.data` happens right here, off the
        // caller's thread — that's the whole point of this queue.
        fwrite(job.data, 1, job.bytes, pipe_);
        job.release();
    }

    std::lock_guard<std::mutex> lock(doneMutex_);
    workerDone_ = true;
    doneCv_.notify_all();
}

void Recorder::Stop() {
    if (!pipe_) return;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopping_ = true;
    }
    queueCv_.notify_all();

    // Bound how long we wait for the worker to drain cleanly. A stalled ffmpeg (full disk,
    // suspended process, stuck writing to a network filesystem) can leave it blocked inside
    // fwrite() forever; every caller of Stop() (the Cancel button, the length-cap auto-stop, and
    // ~Recorder() at process exit) runs on the main thread, so an unbounded join() here would
    // freeze the whole app instead of just abandoning this one recording.
    constexpr auto kDrainTimeout = std::chrono::seconds(5);
    bool drained;
    {
        std::unique_lock<std::mutex> lock(doneMutex_);
        drained = doneCv_.wait_for(lock, kDrainTimeout, [&] { return workerDone_; });
    }

    if (!drained) {
        fprintf(stderr, "Recorder: ffmpeg pipe stalled, abandoning writer thread\n");
        worker_.detach(); // still owns pipe_; leaked rather than closed out from under it
        pipe_ = nullptr;
        queue_.clear();
        return;
    }

    if (worker_.joinable()) worker_.join(); // known to have already returned, so this is immediate

#ifdef _WIN32
    _pclose(pipe_);
#else
    pclose(pipe_);
#endif
    pipe_ = nullptr;
    queue_.clear();
}
