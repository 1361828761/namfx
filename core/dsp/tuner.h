#pragma once

#include <cstddef>
#include <vector>

namespace namfx {
namespace dsp {

// Monophonic tuner core (PLAN G1): autocorrelation-based pitch detection
// with parabolic interpolation and a voicedness threshold. Runs on the audio
// thread (zero allocation, fixed buffers); results are read by the UI layer
// (desktop tuner view, embedded LCD later) and reusable by the harmonizer
// (YIN upgrade path stays open).
class Tuner {
public:
    void prepare(double sampleRate);

    // audio thread: feed a block (n <= prepared maxBlock)
    void process(const float* in, int n);

    // UI/control thread reads
    bool noteDetected() const { return detected_; }
    float frequency() const { return freq_; } // Hz, 0 when undetected
    int midiNote() const { return midiNote_; }
    float cents() const { return cents_; } // -50..+50
    bool isStable() const { return stable_; }

private:
    void detect(); // run the ACF over the ring buffer

    double sampleRate_ = 48000.0;
    int maxBlock_ = 1;
    std::vector<float> buf_; // ring buffer
    std::vector<float> acf_; // autocorrelation workspace (reused)
    std::vector<float> frame_; // analysis frame (reused, no audio-thread alloc)
    std::vector<double> cum_; // squared-sample prefix sums (reused)
    std::size_t write_ = 0;
    std::size_t filled_ = 0;
    int samplesSinceDetect_ = 0;
    int detectEvery_ = 1024;
    bool detected_ = false;
    float freq_ = 0.0f;
    int midiNote_ = 0;
    float cents_ = 0.0f;
    bool stable_ = false;
    float lastFreq_ = 0.0f;
    int stableCount_ = 0;
};

} // namespace dsp
} // namespace namfx
