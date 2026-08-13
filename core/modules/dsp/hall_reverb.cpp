#include "modules/dsp/hall_reverb.h"

#include "modules/module_registry.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

namespace {
// Freeverb tuning (Jezar/Dreampoint 2000, public domain, 44.1 kHz reference)
constexpr int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllpassTuning[4] = {556, 441, 341, 225};
constexpr float kFixedGain = 0.025f;
constexpr float kScaleWet = 3.0f;
constexpr float kScaleDry = 2.0f;
constexpr float kScaleRoom = 0.30f;
constexpr float kOffsetRoom = 0.70f;
constexpr float kScaleDamp = 0.40f;
constexpr float kAllpassG = 0.5f;
} // namespace

void registerHallReverb(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"room", "Room", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"damp", "Damp", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"mix", "Mix", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("rvb.hall", "pedal", std::move(specs),
                            [] { return std::make_unique<HallReverbModule>(); });
}

void HallReverbModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));

    // delay values scale with the sample rate (tuning.h notes 96k needs it)
    const double scale = f / 44100.0;
    for (int k = 0; k < 8; ++k) {
        const std::size_t size = static_cast<std::size_t>(kCombTuning[k] * scale) + 1;
        if (combs_[k].buf.size() != size) {
            combs_[k].buf.assign(size, 0.0f);
        }
        combs_[k].idx = 0;
    }
    for (int k = 0; k < 4; ++k) {
        const std::size_t size = static_cast<std::size_t>(kAllpassTuning[k] * scale) + 1;
        if (allpasses_[k].buf.size() != size) {
            allpasses_[k].buf.assign(size, 0.0f);
        }
        allpasses_[k].idx = 0;
    }

    reset();
    roomSm_ = room_;
    dampSm_ = damp_;
    mixSm_ = mix_;
    prepared_ = true;
}

void HallReverbModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        roomSm_ += smoothK_ * (room_ - roomSm_);
        dampSm_ += smoothK_ * (damp_ - dampSm_);
        mixSm_ += smoothK_ * (mix_ - mixSm_);

        const float feedback = kScaleRoom + kOffsetRoom * roomSm_;
        const float damp1 = kScaleDamp * dampSm_;

        const float x = inL[i];
        const float dry = x * kScaleDry;
        const float in = x * kFixedGain;

        float wet = 0.0f;
        for (int k = 0; k < 8; ++k) {
            Comb& comb = combs_[k];
            const std::size_t size = comb.buf.size();
            const float out = comb.buf[static_cast<std::size_t>(comb.idx)];
            comb.filter += damp1 * (out - comb.filter);
            comb.buf[static_cast<std::size_t>(comb.idx)] = in + comb.feedback * comb.filter;
            comb.feedback = feedback;
            comb.damp1 = damp1;
            comb.idx = (comb.idx + 1) % static_cast<int>(size);
            wet += out;
        }

        for (int k = 0; k < 4; ++k) {
            Allpass& ap = allpasses_[k];
            const std::size_t size = ap.buf.size();
            const float bufout = ap.buf[static_cast<std::size_t>(ap.idx)];
            const float y = -kAllpassG * wet + bufout;
            ap.buf[static_cast<std::size_t>(ap.idx)] = wet + kAllpassG * bufout;
            ap.idx = (ap.idx + 1) % static_cast<int>(size);
            wet = y;
        }

        wet *= kScaleWet;
        outL[i] = dry * (1.0f - mixSm_) + wet * mixSm_;
    }
}

void HallReverbModule::reset()
{
    for (int k = 0; k < 8; ++k) {
        std::fill(combs_[k].buf.begin(), combs_[k].buf.end(), 0.0f);
        combs_[k].idx = 0;
        combs_[k].filter = 0.0f;
    }
    for (int k = 0; k < 4; ++k) {
        std::fill(allpasses_[k].buf.begin(), allpasses_[k].buf.end(), 0.0f);
        allpasses_[k].idx = 0;
    }
}

void HallReverbModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void HallReverbModule::setMaxBlock(int)
{
}

void HallReverbModule::setParameter(const std::string& id, float value)
{
    if (id == "room") {
        room_ = value;
    } else if (id == "damp") {
        damp_ = value;
    } else if (id == "mix") {
        mix_ = value;
    }
}

} // namespace namfx
