#pragma once

#include <cmath>

namespace namfx {

// TS808 tone control stage: second-order analog transfer function from
// Yeh/Abel/Smith, DAFx-07 (Eq. 24), digitized with the bilinear transform
// with frequency pre-warping. Runs at the base sample rate.
class TsToneStage {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = static_cast<float>(sampleRate);
        coefSmoothK_ = 1.0f - std::exp(-1.0f / (0.01f * sampleRate_));
        reset();
    }

    void reset()
    {
        x1_ = x2_ = y1_ = y2_ = 0.0f;
        b0_ = b1_ = b2_ = 0.0f;
        a1_ = a2_ = 0.0f;
        setTone(0.5f);
        b0_ = b0Target_;
        b1_ = b1Target_;
        b2_ = b2Target_;
        a1_ = a1Target_;
        a2_ = a2Target_;
    }

    void setTone(float tone01)
    {
        // inverse-log taper, tone01 in [0, 1] -> wiper resistance in [10, 20k]
        float taper = tone01 <= 0.5f ? 1.8f * tone01 : 0.2f * tone01 + 0.8f;
        calcCoefs(10.0f + taper * 19990.0f);
    }

    inline float processSample(float x)
    {
        b0_ += coefSmoothK_ * (b0Target_ - b0_);
        b1_ += coefSmoothK_ * (b1Target_ - b1_);
        b2_ += coefSmoothK_ * (b2Target_ - b2_);
        a1_ += coefSmoothK_ * (a1Target_ - a1_);
        a2_ += coefSmoothK_ * (a2Target_ - a2_);

        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y < 1.0e-15f && y > -1.0e-15f ? 0.0f : y;
        return y;
    }

private:
    void calcCoefs(float rL)
    {
        constexpr float Cs = 0.22e-6f;
        constexpr float Rs = 1.0e3f;
        constexpr float Ri = 10.0e3f;
        constexpr float Cz = 0.22e-6f;
        constexpr float Rz = 220.0f;
        constexpr float Rf = 1.0e3f;
        constexpr float rPot = 20.0e3f;

        const float wp = 1.0f / (Cs * Rs * Ri / (Rs + Ri));

        const float Rl = rL;
        const float Rr = rPot - rL;
        const float Rlp = Rl * Rr / (Rl + Rr);

        const float wz = 1.0f / (Cz * (Rz + Rlp));
        const float Y = (Rl + Rr) * (Rz + Rlp);
        const float X = (Rr / (Rl + Rr)) / ((Rz + Rlp) * Y);
        const float W = Y / (Rl * Rf + Y);
        const float alpha = (Rl * Rf + Y) / (Y * Rs * Cs);

        // analog H(s) = (b2*s^2 + b1*s + b0) / (a2*s^2 + a1*s + a0)
        const float b2s = 0.0f;
        const float b1s = alpha;
        const float b0s = alpha * W * wz;
        const float a2s = 1.0f;
        const float a1s = wp + wz + X;
        const float a0s = wp * wz;

        // bilinear transform with frequency pre-warping (K = wc / tan(wc / (2*fs)))
        const float wc = std::sqrt(wp * wz);
        const float K = wc == 0.0f ? 2.0f * sampleRate_ : wc / std::tan(wc / (2.0f * sampleRate_));
        const float K2 = K * K;

        const float n0 = b2s * K2 + b1s * K + b0s;
        const float n1 = -2.0f * b2s * K2 + 2.0f * b0s;
        const float n2 = b2s * K2 - b1s * K + b0s;
        const float d0 = a2s * K2 + a1s * K + a0s;
        const float d1 = -2.0f * a2s * K2 + 2.0f * a0s;
        const float d2 = a2s * K2 - a1s * K + a0s;

        b0Target_ = n0 / d0;
        b1Target_ = n1 / d0;
        b2Target_ = n2 / d0;
        a1Target_ = d1 / d0;
        a2Target_ = d2 / d0;
    }

    float sampleRate_ = 48000.0f;
    float coefSmoothK_ = 0.0f;

    float b0_ = 0.0f;
    float b1_ = 0.0f;
    float b2_ = 0.0f;
    float a1_ = 0.0f;
    float a2_ = 0.0f;

    float b0Target_ = 0.0f;
    float b1Target_ = 0.0f;
    float b2Target_ = 0.0f;
    float a1Target_ = 0.0f;
    float a2Target_ = 0.0f;

    float x1_ = 0.0f;
    float x2_ = 0.0f;
    float y1_ = 0.0f;
    float y2_ = 0.0f;
};

} // namespace namfx
