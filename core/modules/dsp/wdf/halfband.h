#pragma once

// 4th-order Butterworth lowpass (two cascaded biquads), fc = sampleRate / 4
// in the oversampled domain. Coefficients are fixed: with fc = fs_os / 4 the
// normalized cutoff is always pi/2, independent of the base sample rate.
class HalfbandLowpass {
public:
    void reset()
    {
        z1a_ = z2a_ = z1b_ = z2b_ = 0.0f;
    }

    inline float process(float x)
    {
        float y = b0A_ * x + z1a_;
        z1a_ = b1A_ * x - a1A_ * y + z2a_;
        z2a_ = b2A_ * x - a2A_ * y;

        y = b0B_ * y + z1b_;
        z1b_ = b1B_ * y - a1B_ * y + z2b_;
        z2b_ = b2B_ * y - a2B_ * y;
        return y;
    }

private:
    // Section A: Q = 1.3066, Section B: Q = 0.5412 (4th-order Butterworth split)
    static constexpr float b0A_ = 0.361607f;
    static constexpr float b1A_ = 0.723214f;
    static constexpr float b2A_ = 0.361607f;
    static constexpr float a1A_ = 0.0f;
    static constexpr float a2A_ = 0.446437f;

    static constexpr float b0B_ = 0.259884f;
    static constexpr float b1B_ = 0.519768f;
    static constexpr float b2B_ = 0.259884f;
    static constexpr float a1B_ = 0.0f;
    static constexpr float a2B_ = 0.0395505f;

    float z1a_ = 0.0f;
    float z2a_ = 0.0f;
    float z1b_ = 0.0f;
    float z2b_ = 0.0f;
};
