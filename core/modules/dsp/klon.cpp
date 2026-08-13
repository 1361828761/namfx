#include "modules/dsp/klon.h"

#include "modules/module_registry.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerTransparent(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"gain", "Gain", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"treble", "Treble", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("od.transparent", "pedal", std::move(specs),
                            [] { return std::make_unique<TransparentModule>(); });
}

void TransparentModule::Biquad1::prepare(double fs)
{
    (void)fs;
    reset();
}

void TransparentModule::Biquad1::reset()
{
    x1_ = 0.0f;
    y1_ = 0.0f;
}

void TransparentModule::Biquad1::setCoefs(float b0, float b1, float a1)
{
    b0_ = b0;
    b1_ = b1;
    a1_ = a1;
}

void TransparentModule::Biquad2::prepare(double fs)
{
    (void)fs;
    reset();
}

void TransparentModule::Biquad2::reset()
{
    x1_ = x2_ = y1_ = y2_ = 0.0f;
}

void TransparentModule::Biquad2::setCoefs(float b0, float b1, float b2, float a1, float a2)
{
    b0_ = b0;
    b1_ = b1;
    b2_ = b2;
    a1_ = a1;
    a2_ = a2;
}

void TransparentModule::prepare(double sampleRate, int)
{
    sampleRate_ = sampleRate;

    preAmp_.prepare(static_cast<float>(sampleRate));
    clipping_.prepare(static_cast<float>(sampleRate));
    feedForward2_.prepare(static_cast<float>(sampleRate));

    inputBuffer_.prepare(sampleRate);
    ampStage_.prepare(sampleRate);
    summingAmp_.prepare(sampleRate);
    tone_.prepare(sampleRate);
    outputStage_.prepare(sampleRate);
    dcBlocker_.prepare(sampleRate);

    preAmp_.setGain(gain_);
    feedForward2_.setGain(gain_);
    updateAmpStageCoefs((1.0f - gain_) * 100000.0f + 2000.0f);
    updateToneCoefs(treble_);
    updateOutputCoefs(level_);

    const float f = static_cast<float>(sampleRate);
    const float k2f = 2.0f * f;
    constexpr double kTwoPi = 6.28318530717958647692;
    dcBlocker_.setCoefs(1.0f, -1.0f, -static_cast<float>(std::exp(-kTwoPi * 35.0 / sampleRate)));

    // input buffer: H(s) = C1*R2*s / (C1*(R1+R2)*s + 1), K = 2*fs
    const float c1r2 = 0.1e-6f * 1000000.0f;
    const float a0in = 0.1e-6f * (10000.0f + 1000000.0f);
    const float d0in = a0in * k2f + 1.0f;
    inputBuffer_.setCoefs(c1r2 * k2f / d0in, -c1r2 * k2f / d0in, (1.0f - a0in * k2f) / d0in);

    // summing amp: H(s) = R20 / (C13*R20*s + 1), K = 2*fs
    const float c13r20 = 820.0e-12f * 392000.0f;
    const float d0sum = c13r20 * k2f + 1.0f;
    summingAmp_.setCoefs(392000.0f / d0sum, 392000.0f / d0sum, (1.0f - c13r20 * k2f) / d0sum);

    prepared_ = true;
}

void TransparentModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        float x = inL[i] * 0.5f;
        x = inputBuffer_.process(x);
        x = x < -4.5f ? -4.5f : (x > 4.5f ? 4.5f : x);

        preAmp_.processSample(x);
        x = ampStage_.process(x);
        x = x < -4.5f ? -4.5f : (x > 4.5f ? 4.5f : x);

        const float clipped = clipping_.processSample(x);
        const float ff2 = feedForward2_.processSample(x);

        x = clipped + preAmp_.getFF1() + ff2;
        x = summingAmp_.process(x);
        x = x < -13.1f ? -13.1f : (x > 11.7f ? 11.7f : x);

        x = tone_.process(x);
        x = -x;
        x = x < -13.1f ? -13.1f : (x > 11.7f ? 11.7f : x);

        x = outputStage_.process(x);
        x = dcBlocker_.process(x);
        outL[i] = x;
    }
}

void TransparentModule::reset()
{
    preAmp_.reset();
    clipping_.reset();
    feedForward2_.reset();
    inputBuffer_.reset();
    ampStage_.reset();
    summingAmp_.reset();
    tone_.reset();
    outputStage_.reset();
    dcBlocker_.reset();
}

void TransparentModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void TransparentModule::setMaxBlock(int)
{
}

void TransparentModule::setParameter(const std::string& id, float value)
{
    if (id == "gain") {
        gain_ = value;
        preAmp_.setGain(gain_);
        feedForward2_.setGain(gain_);
        updateAmpStageCoefs((1.0f - gain_) * 100000.0f + 2000.0f);
    } else if (id == "treble") {
        treble_ = value;
        updateToneCoefs(treble_);
    } else if (id == "level") {
        level_ = value;
        updateOutputCoefs(level_);
    }
}

void TransparentModule::updateAmpStageCoefs(float r10b)
{
    constexpr float C7 = 82.0e-9f;
    constexpr float C8 = 390.0e-12f;
    constexpr float R11 = 15.0e3f;
    constexpr float R12 = 422.0e3f;
    const float f = static_cast<float>(sampleRate_);

    const float a0 = C7 * C8 * r10b * R11 * R12;
    const float a1 = C7 * r10b * R11 + C8 * R12 * (r10b + R11);
    const float a2 = r10b + R11;
    const float b0 = a0;
    const float b1 = C7 * R11 * R12 + a1;
    const float b2 = R12 + a2;

    const float wc = std::sqrt(a2 / a0);
    const float K = wc == 0.0f ? 2.0f * f : wc / std::tan(wc / (2.0f * f));
    const float K2 = K * K;

    const float n0 = b0 * K2 + b1 * K + b2;
    const float n1 = -2.0f * b0 * K2 + 2.0f * b2;
    const float n2 = b0 * K2 - b1 * K + b2;
    const float d0 = a0 * K2 + a1 * K + a2;
    const float d1 = -2.0f * a0 * K2 + 2.0f * a2;
    const float d2 = a0 * K2 - a1 * K + a2;

    ampStage_.setCoefs(n0 / d0, n1 / d0, n2 / d0, d1 / d0, d2 / d0);
}

void TransparentModule::updateToneCoefs(float treble)
{
    constexpr float Rpot = 10.0e3f;
    constexpr float C = 3.9e-9f;
    constexpr float G1 = 1.0f / 100.0e3f;
    const float G2 = 1.0f / (1.8e3f + (1.0f - treble) * Rpot);
    const float G3 = 1.0f / (4.7e3f + treble * Rpot);
    constexpr float G4 = 1.0f / 100.0e3f;
    const float f = static_cast<float>(sampleRate_);

    const float wc = G1 / C;
    const float K = wc / std::tan(wc / (2.0f * f));

    // H(s) = (b1*s + b0) / (a1*s + a0), then ChowCentaur flips the pole
    const float b1s = C * (G1 + G2);
    const float b0s = G1 * (G2 + G3);
    const float a1s = C * (G3 - G4);
    const float a0s = -G4 * (G2 + G3);

    const float n0 = b1s * K + b0s;
    const float n1 = b0s - b1s * K;
    const float d0 = a1s * K + a0s;
    const float d1 = a0s - a1s * K;

    tone_.setCoefs(n0 / d1, n1 / d1, d0 / d1);
}

void TransparentModule::updateOutputCoefs(float level)
{
    const float R1 = 560.0f + (1.0f - level) * 10000.0f;
    const float R2 = level * 10000.0f + 1.0f;
    constexpr float C1 = 4.7e-6f;
    const float f = static_cast<float>(sampleRate_);
    const float k2f = 2.0f * f;

    // H(s) = C1*R2*s / (C1*(R1+R2)*s + 1), K = 2*fs
    const float c1r2 = C1 * R2;
    const float a0 = C1 * (R1 + R2);
    const float d0 = a0 * k2f + 1.0f;

    outputStage_.setCoefs(c1r2 * k2f / d0, -c1r2 * k2f / d0, (1.0f - a0 * k2f) / d0);
}

} // namespace namfx
