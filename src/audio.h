#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class SourceKind {
    None = 0,
    System = 1,
    Application = 2,
};

struct AudioSource {
    SourceKind kind = SourceKind::None;
    DWORD pid = 0;
    std::wstring path;
    std::wstring name;

    bool operator==(const AudioSource& other) const {
        return kind == other.kind && pid == other.pid;
    }
};

struct AudioApp {
    DWORD pid = 0;
    std::wstring name;
    std::wstring exe;
    std::wstring path;
    std::wstring title;
    bool playing = false;
};

enum class CaptureState {
    Idle,
    Connecting,
    Running,
    Error,
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    static std::vector<AudioApp> EnumerateApps();

    bool Start(const AudioSource& source);
    void Stop();

    CaptureState State() const { return state_.load(std::memory_order_relaxed); }
    std::wstring Error() const;
    AudioSource Current() const;
    int SampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }
    float Level() const { return level_.load(std::memory_order_relaxed); }

    // Copies the newest samples in chronological order. Returns the number copied.
    size_t Snapshot(float* out, size_t count) const;
    uint64_t TotalWritten() const;

private:
    void ThreadProc(AudioSource source);
    bool RunCapture(const AudioSource& source, HANDLE stopEvent);
    void SetError(const std::wstring& message);
    void PushSamples(const float* samples, size_t count);
    void ClearAudio();

    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    mutable std::mutex controlMutex_;

    std::atomic<CaptureState> state_{CaptureState::Idle};
    mutable std::mutex errorMutex_;
    std::wstring error_;
    mutable std::mutex sourceMutex_;
    AudioSource current_;

    mutable std::mutex ringMutex_;
    std::vector<float> ring_;
    size_t writePos_ = 0;
    uint64_t totalWritten_ = 0;
    std::atomic<int> sampleRate_{44100};
    std::atomic<float> level_{0.0f};
};
