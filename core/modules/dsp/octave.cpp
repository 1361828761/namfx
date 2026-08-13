#include "modules/dsp/octave.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerOctave(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"mix", "Mix", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"tone", "Tone", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("pitch.octave", "pedal", std::move(specs),
                            [] { return std::make_unique<OctaveModule>(); });
}

void OctaveModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;

    reset();
    mixSm_ = mix_;
    toneSm_ = tone_;
    levelSm_ = level_;
    prepared_ = true;
}

void OctaveModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    constexpr float kTwoPi = 6.28318530717958647692f;
    for (int i = 0; i < n; ++i) {
        mixSm_ += smoothK_ * (mix_ - mixSm_);
        toneSm_ += smoothK_ * (tone_ - toneSm_);
        levelSm_ += smoothK_ * (level_ - levelSm_);

        const float x = inL[i];

        // rising zero crossing (debounced by minimum gap, no amplitude
        // threshold): only updates the smoothed crossing interval estimate
        if (prevX_ <= 0.0f && x > 0.0f && counter_ - lastCrossCounter_ > 4) {
            // linear interpolation of the true crossing between sample i-1
            // (prevX_) and sample i (x)
            const double z = static_cast<double>(counter_) - 1.0
                - static_cast<double>(prevX_) / (static_cast<double>(x) - static_cast<double>(prevX_));
            const double d = z - prevZ_;
            prevZ_ = z;
            if (d > 4.0 && d < 200000.0) {
                dSm_ = 0.95 * dSm_ + 0.05 * d;
            }
            lastCrossCounter_ = counter_;
        }
        prevX_ = x;
        ++counter_;

        // free-running divide-by-two: flip once per smoothed input period;
        // at a constant input the estimate converges and the phase holds
        subPhase_ += static_cast<float>(1.0 / dSm_);
        if (subPhase_ >= 1.0f) {
            subPhase_ -= 1.0f;
            flip_ = -flip_;
        }

        // sub-octave = input waveform times the alternating flip
        const float sub = x * flip_;

        // tone low-pass 300 Hz..2 kHz
        const float fc = 300.0f * std::pow(2000.0f / 300.0f, toneSm_);
        const float w0 = kTwoPi * fc / fs_;
        const float cosW0 = std::cos(w0);
        const float alpha = std::sin(w0) / std::sqrt(2.0f);
        const float a0 = 1.0f + alpha;
        const float b0 = (1.0f - cosW0) * 0.5f / a0;
        const float b1 = (1.0f - cosW0) / a0;
        const float b2 = b0;
        const float a1 = -2.0f * cosW0 / a0;
        const float a2 = (1.0f - alpha) / a0;

        const float lpOut = b0 * sub + b1 * lpX1_ + b2 * lpX2_ - a1 * lpY1_ - a2 * lpY2_;
        lpX2_ = lpX1_;
        lpX1_ = sub;
        lpY2_ = lpY1_;
        lpY1_ = lpOut;

        outL[i] = (x * (1.0f - mixSm_) + lpOut * mixSm_) * levelSm_;
    }
}

void OctaveModule::reset()
{
    flip_ = 1.0f;
    prevX_ = 0.0f;
    prevZ_ = 0.0;
    dSm_ = 100.0;
    subPhase_ = 0.0f;
    lastCrossCounter_ = -100;
    counter_ = 0;
    lpX1_ = 0.0f;
    lpX2_ = 0.0f;
    lpY1_ = 0.0f;
    lpY2_ = 0.0f;
}

void OctaveModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void OctaveModule::setMaxBlock(int)
{
}

void OctaveModule::setParameter(const std::string& id, float value)
{
    if (id == "mix") {
        mix_ = value;
    } else if (id == "tone") {
        tone_ = value;
    } else if (id == "level") {
        level_ = value;
    }
}

} // namespace namfx
