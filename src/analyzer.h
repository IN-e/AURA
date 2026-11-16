#pragma once

#include <array>
#include <cstdint>
#include <vector>

class AudioAnalyzer {
public:
    static constexpr int kBandCount = 160;
    static constexpr int kFftSize = 2048;

    AudioAnalyzer();

    void Reset();
    void Update(const float* samples, size_t count, uint64_t sampleTotal, bool newAudio,
                float dt);

    float BandAt(float position) const;
    float BandCyclic(float position) const;
    float WaveAt(float position) const;
    float TimeDomainAt(float position) const;
    float Level() const { return level_; }
    float RawPeak() const { return rawPeak_; }
    float Flux() const { return flux_; }
    float Beat() const { return beat_; }
    float Gate() const { return gate_; }
    bool HasSignal() const { return level_ > 0.01f; }
    int SampleRate() const { return sampleRate_; }
    void SetSampleRate(int sampleRate);

private:
    void RebuildBandMap();
    void ComputeFft();

    int sampleRate_ = 44100;
    std::vector<float> input_;
    std::vector<float> window_;
    std::vector<float> fftRe_;
    std::vector<float> fftIm_;
    std::vector<float> bands_;
    std::vector<float> bandTargets_;
    std::vector<float> bandVelocities_;
    std::vector<float> bandMap_;
    std::array<float, kBandCount> previousBands_{};
    std::vector<float> wave_;
    std::vector<float> waveTargets_;
    std::vector<float> timeDomain_;
    std::array<float, kFftSize> history_{};
    std::vector<float> historyScratch_;
    size_t historyPos_ = 0;

    float level_ = 0.0f;
    float rawPeak_ = 0.0f;
    float flux_ = 0.0f;
    float gate_ = 0.0f;
    float peakEnvelope_ = 0.0f;
    float waveGain_ = 1.0f;
    float beat_ = 0.0f;
    float shortAverage_ = 0.0f;
    float longAverage_ = 0.0f;
    float elapsed_ = 0.0f;
    float beatCooldown_ = 0.0f;
    float silenceTime_ = 1.0f;
    uint64_t lastTotal_ = UINT64_MAX;
    bool haveHistory_ = false;
    bool dampingInitialized_ = false;
    bool fluxInitialized_ = false;
};
