#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace namfx {

// Variable fractional delay line (BBD-style behavioural model): circular
// buffer with linear interpolation. Allocation happens in prepare (load path),
// the audio callback only reads/writes the buffer.
class FractionalDelay {
public:
    void prepare(double sampleRate, float maxDelayMs)
    {
        sampleRate_ = static_cast<float>(sampleRate);
        const std::size_t size = static_cast<std::size_t>(sampleRate_ * maxDelayMs * 0.001f) + 4;
        if (buf_.size() != size) {
            buf_.resize(size, 0.0f);
        }
        writeIdx_ = 0;
    }

    void reset()
    {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        writeIdx_ = 0;
    }

    void setDelayMs(float ms)
    {
        delaySamples_ = std::max(ms, 0.0f) * 0.001f * sampleRate_;
    }

    inline float process(float x)
    {
        const int size = static_cast<int>(buf_.size());
        buf_[writeIdx_] = x;

        float readPos = static_cast<float>(writeIdx_) - delaySamples_;
        readPos = std::fmod(readPos, static_cast<float>(size));
        if (readPos < 0.0f) {
            readPos += static_cast<float>(size);
        }
        if (readPos >= static_cast<float>(size)) {
            readPos = 0.0f;
        }
        const int i0 = static_cast<int>(readPos);
        const float frac = readPos - static_cast<float>(i0);
        const int i1 = (i0 + 1) % size;

        const float y = buf_[i0] * (1.0f - frac) + buf_[i1] * frac;
        writeIdx_ = (writeIdx_ + 1) % size;
        return y;
    }

private:
    std::vector<float> buf_;
    int writeIdx_ = 0;
    float delaySamples_ = 0.0f;
    float sampleRate_ = 48000.0f;
};

// Triangle LFO in [0, 1]
class TriLfo {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = static_cast<float>(sampleRate);
    }

    void reset()
    {
        phase_ = 0.0f;
    }

    void setRate(float hz)
    {
        rate_ = hz;
    }

    float sampleRate() const
    {
        return sampleRate_;
    }

    inline float process()
    {
        phase_ += rate_ / sampleRate_;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
        }
        return phase_ < 0.5f ? phase_ * 2.0f : (1.0f - phase_) * 2.0f;
    }

private:
    float sampleRate_ = 48000.0f;
    float phase_ = 0.0f;
    float rate_ = 0.5f;
};

} // namespace namfx
