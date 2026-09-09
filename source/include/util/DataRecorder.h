#pragma once
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// One particle's sampled state for a single CSV row. Always fully populated by the caller;
// DataRecorder decides which columns actually get written based on the Fields selected at
// Start() — trades a few unused floats per sample for a caller that doesn't need to know which
// columns are active.
struct ParticleSample {
    float x, y;
    float vx, vy;
    float speed;
    float density;
    float pressure;
};

// Buffers particle-field CSV rows off the sim thread, mirroring Recorder's queue/worker-thread
// shape (see util/Recorder.h): SubmitFrame() only queues a job and returns, a background thread
// does the formatting and disk write. Unlike Recorder there's no external process and no
// caller-owned buffer to release — each Job owns its own copy of the frame's samples via
// std::move — so backpressure just means dropping the frame's samples on the floor rather than
// calling anything back.
class DataRecorder {
public:
    struct Fields {
        bool position = true;
        bool velocity = true;
        bool speed    = false;
        bool density  = true;
        bool pressure = true;
    };

    bool Start(const std::string& outputPath, const Fields& fields);
    void SubmitFrame(int frame, float time, std::vector<ParticleSample> samples);
    void Stop();
    bool IsActive() const { return file_.is_open(); }

    ~DataRecorder();

private:
    static constexpr size_t kMaxQueuedFrames = 4;

    struct Job {
        int                         frame;
        float                       time;
        std::vector<ParticleSample> samples;
    };

    void WriterLoop();
    void WriteJob(const Job& job);

    std::ofstream file_;
    Fields        fields_{};
    std::thread   worker_;

    std::mutex              queueMutex_;
    std::condition_variable queueCv_;
    std::deque<Job>         queue_;
    bool                    stopping_{ false };
};
