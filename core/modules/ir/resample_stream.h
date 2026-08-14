#pragma once

#include "modules/ir/resample.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace namfx {
namespace ir {

// Streaming windowed-sinc resampler (same Kaiser kernel as resampleSinc,
// docs/research/resample_minphase.md). Real-time safe: all state is
// pre-allocated in prepare(), process()/flush() do no allocation.
// Zero-pads before the stream start and after its end; introduces
// `zeros` source samples of latency.
class StreamingResampler
{
public:
    void prepare(double fromRate, double toRate, int maxInputBlock, int zeros = 64)
    {
        if (fromRate <= 0.0 || toRate <= 0.0) {
            fromRate = 1.0;
            toRate = 1.0;
        }
        fromRate_ = fromRate;
        toRate_ = toRate;
        zeros_ = std::max(zeros, 4);
        constexpr double kBeta = 14.0;
        i0b_ = detail::besselI0(kBeta);
        // downsample: kernel cutoff scales to min(1, to/from) x source Nyquist
        cutoff_ = (toRate < fromRate) ? toRate / fromRate : 1.0;
        maxInBlock_ = std::max(maxInputBlock, 1);
        hist_.assign(static_cast<std::size_t>(2 * zeros_) + maxInBlock_, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill(hist_.begin(), hist_.end(), 0.0f);
        histLen_ = 0;
        inTotal_ = 0;
        outCount_ = 0;
    }

    // Upper bound on output samples for a block of `inputBlock` input
    // samples (call before process to size the output buffer).
    int outCapacity(int inputBlock) const
    {
        const double produced = (static_cast<double>(inTotal_ + inputBlock) + zeros_)
            * (toRate_ / fromRate_) + 1.0;
        return std::max(static_cast<int>(produced) + 1, inputBlock + 1);
    }

    // Consume `n` input samples and produce output into `out` (capacity at
    // least outCapacity(n)); returns the number of output samples written.
    // Oversized blocks are split internally against maxInputBlock so the
    // history buffer can never overflow.
    int process(const float* in, int n, float* out)
    {
        int written = 0;
        while (n > 0) {
            const int sub = std::min(n, maxInBlock_);
            written += processOne(in, sub, out + written);
            in += sub;
            n -= sub;
        }
        return written;
    }

    // Drain remaining output after the stream ends; returns samples written.
    int flush(float* out)
    {
        if (inTotal_ <= 0) {
            return 0;
        }
        const long last = static_cast<long>(static_cast<double>(inTotal_) * toRate_ / fromRate_);
        int produced = 0;
        while (outCount_ <= last) {
            out[produced++] = sampleAt(outCount_);
            ++outCount_;
        }
        return produced;
    }

    // Source samples of latency introduced (initial zero-pad region)
    double latencySamples() const
    {
        return static_cast<double>(zeros_) * fromRate_ / toRate_;
    }

private:
    int processOne(const float* in, int n, float* out)
    {
        if (n <= 0) {
            return 0;
        }
        if (histLen_ + n > static_cast<int>(hist_.size())) {
            // keep 2*zeros_ samples: the interpolation window spans
            // [i0-zeros_+1, i0+zeros_] (2*zeros_ samples)
            std::memmove(hist_.data(), hist_.data() + histLen_ - 2 * zeros_,
                         static_cast<std::size_t>(2 * zeros_) * sizeof(float));
            histLen_ = 2 * zeros_;
        }
        std::memcpy(hist_.data() + histLen_, in, static_cast<std::size_t>(n) * sizeof(float));
        histLen_ += n;
        inTotal_ += n;
        return produce(out);
    }

    int produce(float* out)
    {
        int produced = 0;
        for (;;) {
            const double pos = static_cast<double>(outCount_) * fromRate_ / toRate_;
            const long i0 = static_cast<long>(pos);
            // kernel needs stream sample i0+zeros_ (samples below 0 are zero);
            // stop until it has arrived
            if (i0 + zeros_ >= inTotal_) {
                break;
            }
            out[produced++] = sampleAt(outCount_);
            ++outCount_;
        }
        return produced;
    }

    float sampleAt(long n) const
    {
        constexpr double kPi = 3.14159265358979323846;
        const double pos = static_cast<double>(n) * fromRate_ / toRate_;
        const long i0 = static_cast<long>(pos);
        // hist_ holds stream samples [inTotal_-histLen_, inTotal_-1]
        const long hBase = static_cast<long>(inTotal_) - static_cast<long>(histLen_);
        double acc = 0.0;
        double wsum = 0.0;
        for (long k = i0 - zeros_ + 1; k <= i0 + zeros_; ++k) {
            const long hIdx = k - hBase;
            // stream samples below 0 are outside the signal: skip their
            // kernel contribution (matches the batch resampler, which
            // normalizes over the in-range taps only)
            if (hIdx < 0 || hIdx >= histLen_) {
                continue;
            }
            const double xv = static_cast<double>(hist_[static_cast<std::size_t>(hIdx)]);
            const double d = pos - static_cast<double>(k);
            if (d >= static_cast<double>(zeros_) || d <= -static_cast<double>(zeros_)) {
                continue;
            }
            const double ds = d * cutoff_;
            const double s = (ds == 0.0) ? 1.0 : std::sin(kPi * ds) / (kPi * ds);
            const double w = detail::besselI0(
                kBeta_ * std::sqrt(1.0 - (d / static_cast<double>(zeros_))
                                          * (d / static_cast<double>(zeros_))))
                / i0b_;
            acc += xv * s * w;
            wsum += s * w;
        }
        return static_cast<float>((wsum != 0.0) ? acc / wsum : 0.0);
    }

    double fromRate_ = 1.0;
    double toRate_ = 1.0;
    double cutoff_ = 1.0;
    double i0b_ = 1.0;
    static constexpr double kBeta_ = 14.0;
    int zeros_ = 64;
    int maxInBlock_ = 1;
    std::vector<float> hist_;
    int histLen_ = 0;
    long inTotal_ = 0;
    long outCount_ = 0;
};

} // namespace ir
} // namespace namfx
