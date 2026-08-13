#include "modules/dsp/ocd.h"

#include "modules/module_registry.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerMosfetOd(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"drive", "Drive", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"tone", "Tone", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"volume", "Volume", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("od.mosfet", "pedal", std::move(specs),
                            [] { return std::make_unique<MosfetOdModule>(); });
}

void MosfetOdModule::prepare(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));

    // bass cut (C4 = 1n, equivalent network): one-pole high-pass ~400 Hz.
    // low-pass form here is y = s + a*(x - s) with a = 1 - exp(-2*pi*fc/fs)
    hpA_ = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 400.0f / f);

    reset();
    updateTargets();
    gain1_ = gain1Target_;
    lpA_ = lpATarget_;
    toneA_ = toneATarget_;
    volGain_ = volGainTarget_;
    prepared_ = true;
}

void MosfetOdModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        gain1_ += smoothK_ * (gain1Target_ - gain1_);
        lpA_ += smoothK_ * (lpATarget_ - lpA_);
        toneA_ += smoothK_ * (toneATarget_ - toneA_);
        volGain_ += smoothK_ * (volGainTarget_ - volGain_);

        float x = inL[i];
        const float bass = hpState_ + hpA_ * (x - hpState_);
        hpState_ = bass;
        x -= bass;
        x *= gain1_;
        lpState_ += lpA_ * (x - lpState_);
        x = lpState_;
        x = clip(x);
        // op-amp rail: asymmetric, germanium side clips earlier (v4)
        x = x < -3.5f ? -3.5f : (x > 5.0f ? 5.0f : x);
        x *= 0.185f; // second stage gain (1 + R11/R12 = 1.67) + 9V domain scale
        toneState_ += toneA_ * (x - toneState_);
        x = toneState_;
        outL[i] = x * volGain_;
    }
}

void MosfetOdModule::reset()
{
    lpState_ = 0.0f;
    hpState_ = 0.0f;
    toneState_ = 0.0f;
}

void MosfetOdModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void MosfetOdModule::setMaxBlock(int)
{
}

void MosfetOdModule::setParameter(const std::string& id, float value)
{
    if (id == "drive") {
        drive_ = value;
        updateTargets();
    } else if (id == "tone") {
        tone_ = value;
        updateTargets();
    } else if (id == "volume") {
        volume_ = value;
        updateTargets();
    }
}

void MosfetOdModule::updateTargets()
{
    const float f = static_cast<float>(sampleRate_);
    constexpr double kTwoPi = 6.28318530717958647692;

    // audio taper on the drive pot (A1M)
    const float taper = drive_ <= 0.5f ? 1.8f * drive_ : 0.2f * drive_ + 0.8f;
    const float zf = 10.0e3f + taper * 1.0e6f;

    // gain = 1 + (R8 + pot) / R5, R5 = 4.7k
    gain1Target_ = 1.0f + zf / 4.7e3f;

    // feedback cap C6 = 220p across Zf: high-freq rolloff, floored at 2 kHz
    // so full drive does not kill all treble
    float fc = static_cast<float>(1.0 / (kTwoPi * 220.0e-12 * zf));
    fc = std::max(fc, 2000.0f);
    lpATarget_ = 1.0f - static_cast<float>(std::exp(-kTwoPi * static_cast<double>(fc) / f));

    // treble bleed: B10K pot + C8 = 47n to ground, tone = 1 passes highs
    const float rs = 100.0f;
    const float fcTone = static_cast<float>(1.0 / (kTwoPi * ((1.0f - tone_) * 10.0e3f + rs) * 47.0e-9));
    toneATarget_ = 1.0f - static_cast<float>(std::exp(-kTwoPi * static_cast<double>(fcTone) / f));

    // volume pot, audio taper (square)
    volGainTarget_ = volume_ * volume_;
}

} // namespace namfx
