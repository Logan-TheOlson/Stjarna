#pragma once
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// Pipes raw pixel frames to an external ffmpeg process over stdin, which encodes them to an mp4
// as they arrive. ffmpeg must be reachable on PATH; Start() fails quietly (IsActive() stays false)
// if the pipe can't be opened, so callers can treat a missing ffmpeg as "recording is a no-op"
// rather than special-casing it. If ffmpeg dies mid-recording instead, SIGPIPE is ignored (see
// Start()) so the write that discovers this fails with EPIPE rather than killing the process, and
// Stop() gives up waiting on a stalled pipe after a few seconds rather than hanging forever.
//
// SubmitFrame() only queues a job and returns — the background thread does the (potentially slow:
// `data` is typically GPU-visible memory read back over PCIe) read and the write to ffmpeg's
// stdin, then calls `release` so the caller can reuse the buffer `data` points to. The caller must
// keep that memory valid and untouched until `release` fires. If the queue backs up past
// kMaxQueuedFrames, new frames are dropped (their `release` is called immediately, without being
// read) rather than queued without bound — an encoder that can't keep up skips frames, it never
// stalls the caller.
class Recorder {
public:
    bool Start(const std::string& outputPath, int width, int height, int fps, const char* pixelFormat);
    void SubmitFrame(const void* data, size_t bytes, std::function<void()> release);
    void Stop();
    bool IsActive() const { return pipe_ != nullptr; }

    ~Recorder();

private:
    static constexpr size_t kMaxQueuedFrames = 4;

    struct Job {
        const void*           data;
        size_t                bytes;
        std::function<void()> release;
    };

    void WriterLoop();

    FILE*       pipe_{ nullptr };
    std::thread worker_;

    std::mutex              queueMutex_;
    std::condition_variable queueCv_;
    std::deque<Job>         queue_;
    bool                    stopping_{ false };

    // Signaled by WriterLoop right before it returns, so Stop() can bound how long it waits for a
    // clean drain — a stalled ffmpeg (blocked mid-write, e.g. a full disk) can leave the worker
    // stuck inside fwrite() indefinitely, and there's no portable way to cancel that blocking call.
    std::mutex              doneMutex_;
    std::condition_variable doneCv_;
    bool                    workerDone_{ false };
};
