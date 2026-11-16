#include "analyzer.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;

float ClampF(float value, float low, float high) {
    return std::max(low, std::min(high, value));
}

float Damp(float current, float target, float rate, float dt) {
    float alpha = 1.0f - std::exp(-rate * dt);
    return current + (target - current) * ClampF(alpha, 0.0f, 1.0f);
}
}  // namespace

AudioAnalyzer::AudioAnalyzer()
    : input_(kFftSize, 0.0f),
      window_(kFftSize, 0.0f),
      fftRe_(kFftSize, 0.0f),
      fftIm_(kFftSize, 0.0f),
      bands_(kBandCount, 0.0f),
      bandTargets_(kBandCount, 0.0f),
      bandVelocities_(kBandCount, 0.0f),
      bandMap_(kBandCount, 0.0f),
      wave_(kFftSize, 0.0f),
      waveTargets_(kFftSize, 0.0f),
      timeDomain_(kFftSize, 0.0f),
      historyScratch_(kBandCount, 0.0f) {
    for (int i = 0; i < kFftSize; ++i) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * kPi * i / (kFftSize - 1)));
    }
    RebuildBandMap();
    Reset();
}

void AudioAnalyzer::SetSampleRate(int sampleRate) {
    if (sampleRate < 8000) sampleRate = 44100;
    if (sampleRate == sampleRate_) return;
    sampleRate_ = sampleRate;
    RebuildBandMap();
    Reset();
}

void AudioAnalyzer::Reset() {
    std::fill(bands_.begin(), bands_.end(), 0.0f);
    std::fill(bandTargets_.begin(), bandTargets_.end(), 0.0f);
    std::fill(bandVelocities_.begin(), bandVelocities_.end(), 0.0f);
    std::fill(wave_.begin(), wave_.end(), 0.0f);
    std::fill(waveTargets_.begin(), waveTargets_.end(), 0.0f);
    std::fill(timeDomain_.begin(), timeDomain_.end(), 0.0f);
    std::fill(input_.begin(), input_.end(), 0.0f);
    std::fill(history_.begin(), history_.end(), 0.0f);
    previousBands_.fill(0.0f);
    level_ = 0.0f;
    rawPeak_ = 0.0f;
    flux_ = 0.0f;
    gate_ = 0.0f;
    peakEnvelope_ = 0.0f;
    waveGain_ = 1.0f;
    beat_ = 0.0f;
    shortAverage_ = 0.0f;
    longAverage_ = 0.0f;
    elapsed_ = 0.0f;
    beatCooldown_ = 0.0f;
    silenceTime_ = 1.0f;
    lastTotal_ = UINT64_MAX;
    haveHistory_ = false;
    dampingInitialized_ = false;
    fluxInitialized_ = false;
    historyPos_ = 0;
}

void AudioAnalyzer::RebuildBandMap() {
    const float minFrequency = 38.0f;
    const float maxFrequency =
        std::min(17000.0f, sampleRate_ * 0.46f);
    for (int i = 0; i < kBandCount; ++i) {
        float t = static_cast<float>(i) / (kBandCount - 1);
        float frequency = minFrequency * std::pow(maxFrequency / minFrequency, t);
        bandMap_[i] = frequency * kFftSize / sampleRate_;
    }
}

void AudioAnalyzer::Update(const float* samples, size_t count, uint64_t sampleTotal,
                           bool newAudio, float dt) {
    dt = ClampF(dt, 0.001f, 0.12f);
    elapsed_ += dt;
    beatCooldown_ = std::max(0.0f, beatCooldown_ - dt);

    bool fresh = newAudio || !haveHistory_ || sampleTotal != lastTotal_;
    if (sampleTotal != UINT64_MAX) {
        lastTotal_ = sampleTotal;
        haveHistory_ = true;
    }

    if (fresh && samples && count > 0) {
        silenceTime_ = 0.0f;
        std::fill(input_.begin(), input_.end(), 0.0f);
        size_t amount = std::min(count, static_cast<size_t>(kFftSize));
        std::copy(samples + (count - amount), samples + count,
                  input_.begin() + (kFftSize - amount));

        double sumSquares = 0.0;
        float peak = 0.0f;
        for (size_t i = count - amount; i < count; ++i) {
            float value = samples[i];
            sumSquares += static_cast<double>(value) * value;
            peak = std::max(peak, std::fabs(value));
        }
        float rms = static_cast<float>(std::sqrt(sumSquares / std::max<size_t>(amount, 1)));

        if (peak > peakEnvelope_) peakEnvelope_ = peak;
        else peakEnvelope_ = std::max(peak, peakEnvelope_ * 0.982f);
        rawPeak_ = Damp(rawPeak_, peak, peak > rawPeak_ ? 55.0f : 7.0f, dt);
        float desiredGain = 0.72f / std::max(peakEnvelope_, 0.0015f);
        waveGain_ = desiredGain;
        waveGain_ = ClampF(waveGain_, 0.75f, 24.0f);

        float targetGate = ClampF((rms - 0.0007f) / 0.009f, 0.0f, 1.0f);
        targetGate = std::sqrt(targetGate);
        gate_ = Damp(gate_, targetGate, targetGate > gate_ ? 21.0f : 6.5f, dt);

        ComputeFft();

        shortAverage_ = shortAverage_ * 0.82f + rms * 0.18f;
        longAverage_ = longAverage_ * 0.995f + rms * 0.005f;
        if (beatCooldown_ <= 0.0f && shortAverage_ > 0.0035f &&
            shortAverage_ > longAverage_ * 1.32f) {
            beat_ = std::max(beat_, 0.72f);
            beatCooldown_ = 0.20f;
        }

        float gain = std::min(9000.0f, waveGain_) * gate_ * 0.80f;
        for (int i = 0; i < kFftSize; ++i) {
            // Raw time-domain stream, matching analyser.getByteTimeDomainData().
            // No spectrum, auto-gain, or spatial filtering is applied here; the
            // horizontal scope performs its own moving average and dead zone.
            timeDomain_[i] = ClampF(input_[i], -1.0f, 1.0f);
            float value = input_[i] * gain;
            float prev = input_[i > 0 ? i - 1 : 0];
            float next = input_[i + 1 < kFftSize ? i + 1 : kFftSize - 1];
            value += (prev * 0.12f + next * 0.12f) * gain;
            waveTargets_[i] = ClampF(value / 1.24f, -1.0f, 1.0f);
        }

        float targetLevel = std::sqrt(ClampF(rms * 7.5f, 0.0f, 1.0f));
        level_ = Damp(level_, targetLevel, targetLevel > level_ ? 24.0f : 6.0f, dt);
    } else {
        // If audio stops coming altogether, let the visualization settle naturally.
        silenceTime_ += dt;
        if (silenceTime_ > 0.10f) {
            level_ = Damp(level_, 0.0f, 3.6f, dt);
            rawPeak_ = Damp(rawPeak_, 0.0f, 4.0f, dt);
            gate_ = Damp(gate_, 0.0f, 3.0f, dt);
            for (int i = 0; i < kBandCount; ++i) bandTargets_[i] = 0.0f;
            for (int i = 0; i < kFftSize; ++i) {
                waveTargets_[i] = 0.0f;
                timeDomain_[i] = Damp(timeDomain_[i], 0.0f, 8.0f, dt);
            }
        }
    }

    if (!dampingInitialized_) {
        for (int i = 0; i < kBandCount; ++i) {
            bands_[i] = bandTargets_[i] * gate_;
            bandVelocities_[i] = 0.0f;
        }
        wave_ = waveTargets_;
        dampingInitialized_ = true;
    }

    // Critically-damped-ish spring smoothing. This gives the waveform a natural
    // inertia instead of chasing every FFT frame.
    const float spring = 220.0f;
    const float damping = 32.0f;
    float filterDt = std::min(dt, 1.0f / 30.0f);
    for (int i = 0; i < kBandCount; ++i) {
        float target = bandTargets_[i] * gate_;
        float& value = bands_[i];
        float& velocity = bandVelocities_[i];
        float acceleration = (target - value) * spring - velocity * damping;
        velocity += acceleration * filterDt;
        value += velocity * filterDt;
        if (value < 0.0f) {
            value = 0.0f;
            if (velocity < 0.0f) velocity = 0.0f;
        } else if (value > 1.2f) {
            value = 1.2f;
            if (velocity > 0.0f) velocity = 0.0f;
        }
    }

    const float waveAttack = 42.0f;
    const float waveRelease = 24.0f;
    for (int i = 0; i < kFftSize; ++i) {
        float target = waveTargets_[i];
        float rate = std::fabs(target) > std::fabs(wave_[i]) ? waveAttack : waveRelease;
        wave_[i] = Damp(wave_[i], target, rate, dt);
    }

    // Positive spectral flux: how fast the spectrum is changing, not just how
    // loud it is. This drives transient bursts of the outer shards.
    float rising = 0.0f;
    if (fluxInitialized_) {
        for (int i = 0; i < kBandCount; ++i) {
            float delta = bands_[i] - previousBands_[i];
            if (delta > 0.0f) rising += delta;
        }
        rising = ClampF(rising / kBandCount * 9.0f, 0.0f, 1.0f);
    }
    std::copy(bands_.begin(), bands_.end(), previousBands_.begin());
    fluxInitialized_ = true;
    flux_ = Damp(flux_, rising, rising > flux_ ? 34.0f : 4.5f, dt);
    beat_ = std::max(0.0f, beat_ - dt * 4.6f);
}

void AudioAnalyzer::ComputeFft() {
    for (int i = 0; i < kFftSize; ++i) {
        fftRe_[i] = input_[i] * window_[i];
        fftIm_[i] = 0.0f;
    }

    for (int i = 1, j = 0; i < kFftSize; ++i) {
        int bit = kFftSize >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(fftRe_[i], fftRe_[j]);
            std::swap(fftIm_[i], fftIm_[j]);
        }
    }

    for (int len = 2; len <= kFftSize; len <<= 1) {
        float angle = -2.0f * kPi / len;
        float wReal = std::cos(angle);
        float wImag = std::sin(angle);
        for (int i = 0; i < kFftSize; i += len) {
            float curReal = 1.0f;
            float curImag = 0.0f;
            for (int j = 0; j < len / 2; ++j) {
                float uReal = fftRe_[i + j];
                float uImag = fftIm_[i + j];
                int k = i + j + len / 2;
                float vReal = fftRe_[k] * curReal - fftIm_[k] * curImag;
                float vImag = fftRe_[k] * curImag + fftIm_[k] * curReal;
                fftRe_[i + j] = uReal + vReal;
                fftIm_[i + j] = uImag + vImag;
                fftRe_[k] = uReal - vReal;
                fftIm_[k] = uImag - vImag;
                float nextReal = curReal * wReal - curImag * wImag;
                curImag = curReal * wImag + curImag * wReal;
                curReal = nextReal;
            }
        }
    }

    float peakDb = -120.0f;
    for (int band = 0; band < kBandCount; ++band) {
        float bin = bandMap_[band];
        int low = static_cast<int>(std::floor(bin));
        int high = low + 1;
        low = std::clamp(low, 1, kFftSize / 2 - 1);
        high = std::clamp(high, 1, kFftSize / 2 - 1);
        float frac = bin - static_cast<float>(low);
        auto magnitude = [&](int index) {
            float re = fftRe_[index];
            float im = fftIm_[index];
            return std::sqrt(re * re + im * im);
        };
        float mag = magnitude(low) + (magnitude(high) - magnitude(low)) * frac;
        float frequency = bandMap_[band] * sampleRate_ / kFftSize;
        float tilt = 9.0f * std::log10(std::max(frequency, 100.0f) / 100.0f);
        float db = 20.0f * std::log10(mag + 1e-7f) + tilt;
        bandTargets_[band] = db;
        peakDb = std::max(peakDb, db);
    }

    // Shift the range so the strongest band sits near the top of the visual range.
    for (int band = 0; band < kBandCount; ++band) {
        float relative = bandTargets_[band] - peakDb;
        float value = ClampF((relative + 46.0f) / 46.0f, 0.0f, 1.0f);
        value = std::pow(value, 1.35f);
        bandTargets_[band] = value;
    }
}

float AudioAnalyzer::BandAt(float position) const {
    float t = ClampF(position, 0.0f, 1.0f) * (kBandCount - 1);
    int low = static_cast<int>(std::floor(t));
    int high = std::min(low + 1, kBandCount - 1);
    float frac = t - low;
    return bands_[low] + (bands_[high] - bands_[low]) * frac;
}

float AudioAnalyzer::BandCyclic(float position) const {
    auto sample = [&](float p) {
        p -= std::floor(p);
        float scaled = p * kBandCount;
        int low = static_cast<int>(std::floor(scaled));
        int high = (low + 1) % kBandCount;
        float frac = scaled - static_cast<float>(low);
        low %= kBandCount;
        if (low < 0) low += kBandCount;
        return bands_[low] + (bands_[high] - bands_[low]) * frac;
    };
    // A tiny circular pre-filter hides the seam where the highest frequency
    // meets the lowest one, while keeping the full spectrum asymmetric.
    const float spread = 0.006f;
    return sample(position) * 0.5f + sample(position - spread) * 0.25f +
           sample(position + spread) * 0.25f;
}

float AudioAnalyzer::WaveAt(float position) const {
    float t = ClampF(position, 0.0f, 1.0f) * (kFftSize - 1);
    int low = static_cast<int>(std::floor(t));
    int high = std::min(low + 1, kFftSize - 1);
    float frac = t - low;
    return wave_[low] + (wave_[high] - wave_[low]) * frac;
}

float AudioAnalyzer::TimeDomainAt(float position) const {
    float t = ClampF(position, 0.0f, 1.0f) * (kFftSize - 1);
    int low = static_cast<int>(std::floor(t));
    int high = std::min(low + 1, kFftSize - 1);
    float frac = t - low;
    return timeDomain_[low] + (timeDomain_[high] - timeDomain_[low]) * frac;
}
