#include "dsp/tuner.h"

#include <algorithm>
#include <cmath>

namespace namfx {
namespace dsp {

namespace {

// window size: ~42 ms covers two periods of the lowest tuned note (E2 82 Hz)
constexpr std::size_t kWindow = 2048;
constexpr float kAcfThreshold = 0.85f; // voicedness

} // namespace

void Tuner::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    buf_.assign(kWindow, 0.0f);
    acf_.assign(kWindow / 2 + 4, 0.0f);
    frame_.assign(kWindow, 0.0f);
    cum_.assign(kWindow + 1, 0.0);
    write_ = 0;
    filled_ = 0;
    detected_ = false;
    freq_ = 0.0f;
    lastFreq_ = 0.0f;
    stable_ = false;
    stableCount_ = 0;
    maxBlock_ = 1;
}

void Tuner::process(const float* in, int n)
{
    if (n <= 0) {
        return;
    }
    for (int i = 0; i < n; ++i) {
        buf_[write_] = in[i];
        write_ = (write_ + 1) % kWindow;
        if (filled_ < kWindow) {
            ++filled_;
        }
    }
    samplesSinceDetect_ += n;
    if (samplesSinceDetect_ >= detectEvery_ && filled_ == kWindow) {
        samplesSinceDetect_ = 0;
        detect();
    }
}

void Tuner::detect()
{
    // build a contiguous analysis frame from the ring buffer (reused member)
    for (std::size_t i = 0; i < kWindow; ++i) {
        frame_[i] = buf_[(write_ + i) % kWindow];
    }
    const double mean = 0.0;
    double energy = 0.0;
    for (std::size_t i = 0; i < kWindow; ++i) {
        const double v = static_cast<double>(frame_[i]) - mean;
        energy += v * v;
    }
    if (energy < 1e-6) {
        detected_ = false;
        freq_ = 0.0f;
        stableCount_ = 0;
        stable_ = false;
        return;
    }
    // normalized autocorrelation over candidate lags (E2..E6); overlap-window
    // normalization (prefix sums) so large lags are not underestimated.
    // acf_[i] = norm(lag = minLag-1+i) so every candidate has both neighbors
    const int minLag = static_cast<int>(sampleRate_ / 1319.0); // E6
    const int maxLag = static_cast<int>(sampleRate_ / 82.0);   // E2
    // prefix sums of squared samples
    for (std::size_t i = 0; i < kWindow; ++i) {
        const double v = static_cast<double>(frame_[i]);
        cum_[i + 1] = cum_[i] + v * v;
    }
    double best = 0.0;
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag) {
        double acc = 0.0;
        for (std::size_t i = 0; i + static_cast<std::size_t>(lag) < kWindow; ++i) {
            acc += static_cast<double>(frame_[i]) * frame_[i + static_cast<std::size_t>(lag)];
        }
        const double e0 = cum_[kWindow - static_cast<std::size_t>(lag)];
        const double e1 = cum_[kWindow] - cum_[static_cast<std::size_t>(lag)];
        const double norm = acc / std::sqrt(e0 * e1);
        acf_[static_cast<std::size_t>(lag - (minLag - 1))] = static_cast<float>(norm);
        best = std::max(best, norm);
    }
    if (best < kAcfThreshold) {
        detected_ = false;
        freq_ = 0.0f;
        stableCount_ = 0;
        stable_ = false;
        return;
    }
    // pick the SMALLEST-lag local maximum above the threshold: the
    // fundamental (a harmonic/subharmonic lag can score marginally higher
    // when the pitch is not an integer number of samples, e.g. E6 at
    // 1318.5 Hz -> lag 182 beats lag 36)
    const int span = maxLag - minLag + 1;
    int bestLag = -1;
    for (int off = 1; off <= span; ++off) {
        const float v = acf_[static_cast<std::size_t>(off)];
        if (v >= kAcfThreshold && v >= acf_[static_cast<std::size_t>(off - 1)]
            && v >= acf_[static_cast<std::size_t>(off + 1)]) {
            bestLag = (minLag - 1) + off;
            break;
        }
    }
    if (bestLag < 0) {
        // no distinct local peak above the threshold: fall back to the
        // first lag that reaches the global max
        for (int off = 1; off <= span; ++off) {
            if (acf_[static_cast<std::size_t>(off)] > best - 1e-6f) {
                bestLag = (minLag - 1) + off;
                break;
            }
        }
    }
    // parabolic interpolation around the peak lag
    const std::size_t idx = static_cast<std::size_t>(bestLag - (minLag - 1));
    const float y0 = idx > 0 ? acf_[idx - 1] : acf_[idx];
    const float y1 = acf_[idx];
    const float y2 = idx + 1 < acf_.size() ? acf_[idx + 1] : acf_[idx];
    const float denom = y0 - 2.0f * y1 + y2;
    float delta = 0.0f;
    if (std::fabs(denom) > 1e-9f) {
        delta = 0.5f * (y0 - y2) / denom;
    }
    const double lag = static_cast<double>(bestLag) + static_cast<double>(delta);
    const double freq = sampleRate_ / lag;
    // EMA across detections: the display value settles instead of jittering
    // with the analysis-window phase
    freq_ = (lastFreq_ > 0.0f) ? static_cast<float>(0.6 * freq + 0.4 * lastFreq_)
                               : static_cast<float>(freq);
    detected_ = true;

    // note + cents (from the smoothed display frequency)
    const double displayFreq = static_cast<double>(freq_);
    const double midi = 69.0 + 12.0 * std::log2(displayFreq / 440.0);
    midiNote_ = static_cast<int>(std::lround(midi));
    const double ref = 440.0 * std::pow(2.0, (static_cast<double>(midiNote_) - 69.0) / 12.0);
    cents_ = static_cast<float>(1200.0 * std::log2(displayFreq / ref));

    // stability: consistent pitch across consecutive detections
    if (std::fabs(freq_ - lastFreq_) / lastFreq_ < 0.01f && lastFreq_ > 0.0f) {
        stableCount_ = std::min(stableCount_ + 1, 10);
    } else {
        stableCount_ = 0;
    }
    lastFreq_ = freq_;
    stable_ = stableCount_ >= 3;
}

} // namespace dsp
} // namespace namfx
