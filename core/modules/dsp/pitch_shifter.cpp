#include "modules/dsp/pitch_shifter.h"

#include "modules/module_registry.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerPitchShifter(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"shift", "Shift", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"mix", "Mix", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("pitch.shift", "pedal", std::move(specs),
                            [] { return std::make_unique<PitchShifterModule>(); });
}

void PitchShifterModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;

    // 20 ms crossfade window, 100 ms max read-behind (docs/research/pitch.md)
    c_ = static_cast<int>(0.02 * f);
    maxDelay_ = static_cast<int>(0.1 * f);
    n_ = maxDelay_ + c_ + 16;
    buf_.assign(static_cast<std::size_t>(n_), 0.0f);

    reset();
    shiftSm_ = shift_;
    mixSm_ = mix_;
    levelSm_ = level_;
    prepared_ = true;
}

void PitchShifterModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        shiftSm_ += smoothK_ * (shift_ - shiftSm_);
        mixSm_ += smoothK_ * (mix_ - mixSm_);
        levelSm_ += smoothK_ * (level_ - levelSm_);

        // shift 0..1 -> -12..+12 semitones -> ratio 0.5..2
        const float ratio = std::pow(2.0f, (-12.0f + 24.0f * shiftSm_) / 12.0f);

        const float x = inL[i];
        buf_[static_cast<std::size_t>(writePtr_)] = x;
        writePtr_ = (writePtr_ + 1) % n_;

        // read-behind distance with wrap
        float dist = static_cast<float>(writePtr_) - readPos_;
        if (dist < 0.0f) {
            dist += static_cast<float>(n_);
        }

        float y;
        if (crossfadeLeft_ > 0) {
            // blend the current tap with the post-jump tap over C samples
            const float fade = 1.0f - static_cast<float>(crossfadeLeft_) / static_cast<float>(c_);
            y = (1.0f - fade) * readAt(readPos_) + fade * readAt(readPos_ + targetOffset_);
            readPos_ += ratio;
            --crossfadeLeft_;
            if (crossfadeLeft_ == 0) {
                readPos_ += targetOffset_;
            }
        } else {
            if (ratio >= 1.0f) {
                // read catches up with the write head: jump back by C, but
                // start early enough that the read never overtakes mid-fade
                if (dist < 2.0f + static_cast<float>(c_) * (ratio - 1.0f)) {
                    crossfadeLeft_ = c_;
                    targetOffset_ = -static_cast<float>(c_);
                }
            } else if (dist > static_cast<float>(maxDelay_) - 4.0f
                - static_cast<float>(c_) * (1.0f - ratio)) {
                // read fell too far behind: jump forward by C
                crossfadeLeft_ = c_;
                targetOffset_ = static_cast<float>(c_);
            }
            y = readAt(readPos_);
            readPos_ += ratio;
        }

        if (readPos_ >= static_cast<float>(n_)) {
            readPos_ -= static_cast<float>(n_);
        } else if (readPos_ < 0.0f) {
            readPos_ += static_cast<float>(n_);
        }

        outL[i] = (x * (1.0f - mixSm_) + y * mixSm_) * levelSm_;
    }
}

float PitchShifterModule::readAt(float pos) const
{
    float p = pos;
    if (p >= static_cast<float>(n_)) {
        p -= static_cast<float>(n_);
    } else if (p < 0.0f) {
        p += static_cast<float>(n_);
    }
    const int i0 = static_cast<int>(p);
    const float frac = p - static_cast<float>(i0);
    const int i1 = (i0 + 1) % n_;
    return buf_[static_cast<std::size_t>(i0)] * (1.0f - frac)
        + buf_[static_cast<std::size_t>(i1)] * frac;
}

void PitchShifterModule::reset()
{
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    writePtr_ = 0;
    // start one crossfade window behind the write head: 20 ms latency at
    // ratio 1, growing toward the max delay for downward shifts
    readPos_ = static_cast<float>(maxDelay_ + 16);
    crossfadeLeft_ = 0;
    targetOffset_ = 0.0f;
}

void PitchShifterModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void PitchShifterModule::setMaxBlock(int)
{
}

void PitchShifterModule::setParameter(const std::string& id, float value)
{
    if (id == "shift") {
        shift_ = value;
    } else if (id == "mix") {
        mix_ = value;
    } else if (id == "level") {
        level_ = value;
    }
}

} // namespace namfx
