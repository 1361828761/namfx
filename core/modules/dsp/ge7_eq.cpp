#include "modules/dsp/ge7_eq.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

namespace {
constexpr int kEqBands = 7;
constexpr float kBandFreqs[kEqBands] = {100.0f, 200.0f, 400.0f,
                                        800.0f, 1600.0f, 3200.0f, 6400.0f};
}

void registerGe7Eq(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"band100", "100Hz", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"band200", "200Hz", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"band400", "400Hz", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"band800", "800Hz", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"band1600", "1.6kHz", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"band3200", "3.2kHz", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"band6400", "6.4kHz", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("eq.ge7", "pedal", std::move(specs),
                            [] { return std::make_unique<Ge7EqModule>(); });
}

void Ge7EqModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;

    constexpr float kTwoPi = 6.28318530717958647692f;
    constexpr float kPeakingQ = 1.1f;
    for (int k = 0; k < kNumBands; ++k) {
        const float w0 = kTwoPi * kBandFreqs[k] / fs_;
        cosW0_[k] = std::cos(w0);
        if (k < kNumBands - 1) {
            alpha_[k] = std::sin(w0) / (2.0f * kPeakingQ);
        } else {
            // high shelf with slope S = 1
            alpha_[k] = std::sin(w0) * 0.5f * std::sqrt(2.0f);
        }
    }

    reset();
    for (int k = 0; k < kNumBands; ++k) {
        bandsSm_[k] = bands_[k];
    }
    levelSm_ = level_;
    prepared_ = true;
}

void Ge7EqModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        levelSm_ += smoothK_ * (level_ - levelSm_);
        const float levelLin = std::pow(10.0f, (-15.0f + 30.0f * levelSm_) / 20.0f);

        float x = inL[i];
        for (int k = 0; k < kNumBands; ++k) {
            bandsSm_[k] += smoothK_ * (bands_[k] - bandsSm_[k]);
            const float db = -15.0f + 30.0f * bandsSm_[k];
            const float a = std::pow(10.0f, db / 40.0f);
            const float c0 = -2.0f * cosW0_[k];

            float b0, b1, b2, a0, a1, a2;
            if (k < kNumBands - 1) {
                // peaking EQ (RBJ), Q = 1.1
                b0 = 1.0f + alpha_[k] * a;
                b1 = c0;
                b2 = 1.0f - alpha_[k] * a;
                a0 = 1.0f + alpha_[k] / a;
                a1 = c0;
                a2 = 1.0f - alpha_[k] / a;
            } else {
                // high shelf (RBJ), slope S = 1
                const float sq = std::sqrt(a);
                b0 = a * ((a + 1.0f) + (a - 1.0f) * cosW0_[k] + 2.0f * sq * alpha_[k]);
                b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosW0_[k]);
                b2 = a * ((a + 1.0f) + (a - 1.0f) * cosW0_[k] - 2.0f * sq * alpha_[k]);
                a0 = (a + 1.0f) - (a - 1.0f) * cosW0_[k] + 2.0f * sq * alpha_[k];
                a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosW0_[k]);
                a2 = (a + 1.0f) - (a - 1.0f) * cosW0_[k] - 2.0f * sq * alpha_[k];
            }

            const float y = (b0 * x + b1 * x1_[k] + b2 * x2_[k] - a1 * y1_[k] - a2 * y2_[k]) / a0;
            x2_[k] = x1_[k];
            x1_[k] = x;
            y2_[k] = y1_[k];
            y1_[k] = y;
            x = y;
        }

        outL[i] = x * levelLin;
    }
}

void Ge7EqModule::reset()
{
    for (int k = 0; k < kNumBands; ++k) {
        x1_[k] = 0.0f;
        x2_[k] = 0.0f;
        y1_[k] = 0.0f;
        y2_[k] = 0.0f;
    }
}

void Ge7EqModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void Ge7EqModule::setMaxBlock(int)
{
}

void Ge7EqModule::setParameter(const std::string& id, float value)
{
    const char* ids[kNumBands] = {"band100", "band200", "band400", "band800",
                                  "band1600", "band3200", "band6400"};
    for (int k = 0; k < kNumBands; ++k) {
        if (id == ids[k]) {
            bands_[k] = value;
            return;
        }
    }
    if (id == "level") {
        level_ = value;
    }
}

} // namespace namfx
