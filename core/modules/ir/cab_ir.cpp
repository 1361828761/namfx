#include "modules/ir/cab_ir.h"

#include "modules/ir/min_phase.h"
#include "modules/ir/resample.h"
#include "modules/module_registry.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerCabIr(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"gain", "Gain", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"lowcut", "Low Cut", 0.0f, 1.0f, 0.0f, "", Taper::Linear});
    specs.push_back(ParamSpec{"highcut", "High Cut", 0.0f, 1.0f, 1.0f, "", Taper::Linear});
    registry.registerModule("cab.ir", "cab", std::move(specs),
                            [] { return std::make_unique<CabIrModule>(); });
}

bool CabIrModule::loadAsset(const std::string& path)
{
    const ir::WavData wav = ir::loadWavFile(path);
    if (!wav.ok || wav.samples.empty() || wav.sampleRate <= 0.0) {
        return false;
    }
    if (wav.samples.size() > kMaxIrSamples) {
        return false; // reject oversized IR, never truncate
    }
    rawIr_ = wav.samples;
    rawRate_ = wav.sampleRate;
    assetLoaded_ = true;
    return true;
}

void CabIrModule::prepare(double sampleRate, int maxBlockSize)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;
    maxBlock_ = std::max(maxBlockSize, 1);

    if (assetLoaded_) {
        ir_ = ir::resampleSinc(rawIr_, rawRate_, sampleRate);
        ir_ = ir::minimumPhase(ir_);
        // normalize peak to 1.0 so the 0 dB default gain is safe for any
        // measured impulse response (measured IR gain is meaningless)
        float peak = 0.0f;
        for (float v : ir_) {
            peak = std::max(peak, std::fabs(v));
        }
        if (peak > 1e-12f) {
            for (float& v : ir_) {
                v /= peak;
            }
        }
    } else {
        ir_.clear();
    }
    if (ir_.empty()) {
        ir_.assign(1, 1.0f); // degenerate unit IR keeps the chain running
    }

    usePartitioned_ = ir_.size() > kDirectLimit;
    if (usePartitioned_) {
        // uniform partition block 1024 (~21 ms latency at 48k), Gardner 1995
        partitioned_.prepare(ir_, 1024);
        blockWet_.assign(static_cast<std::size_t>(maxBlock_), 0.0f);
    } else {
        buf_.assign(ir_.size(), 0.0f);
    }

    reset();
    gainSm_ = gain_;
    lowcutSm_ = lowcut_;
    highcutSm_ = highcut_;
    prepared_ = true;
}

void CabIrModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    const int irLen = static_cast<int>(ir_.size());
    constexpr float kTwoPi = 6.28318530717958647692f;

    float wet = 0.0f;
    if (usePartitioned_) {
        partitioned_.process(inL, n, blockWet_.data());
    }
    for (int i = 0; i < n; ++i) {
        gainSm_ += smoothK_ * (gain_ - gainSm_);
        lowcutSm_ += smoothK_ * (lowcut_ - lowcutSm_);
        highcutSm_ += smoothK_ * (highcut_ - highcutSm_);
        // gain 0..1 -> -12..+12 dB (0.5 = 0 dB neutral)
        const float gainLin = std::pow(10.0f, (-12.0f + 24.0f * gainSm_) / 20.0f);

        if (usePartitioned_) {
            wet = blockWet_[static_cast<std::size_t>(i)];
        } else {
            const float x = inL[i];
            buf_[static_cast<std::size_t>(bufPos_)] = x;
            bufPos_ = (bufPos_ + 1) % irLen;

            float acc = 0.0f;
            int readPos = bufPos_ - 1;
            if (readPos < 0) {
                readPos += irLen;
            }
            for (int k = 0; k < irLen; ++k) {
                acc += ir_[static_cast<std::size_t>(k)] * buf_[static_cast<std::size_t>(readPos)];
                --readPos;
                if (readPos < 0) {
                    readPos += irLen;
                }
            }
            wet = acc;
        }

        // tone shaping (2nd order Butterworth), fully bypassed at the
        // default settings so the kernel passes exactly
        float shaped = wet;
        if (lowcutSm_ > 0.001f) {
            const float hpFc = 20.0f * std::pow(10.0f, 2.0f * lowcutSm_);
            const float hpW0 = kTwoPi * hpFc / fs_;
            const float hpCos = std::cos(hpW0);
            const float hpAlpha = std::sin(hpW0) / std::sqrt(2.0f);
            const float hpA0 = 1.0f + hpAlpha;
            const float hpb0 = (1.0f + hpCos) * 0.5f / hpA0;
            const float hpb1 = -(1.0f + hpCos) / hpA0;
            const float hpb2 = (1.0f + hpCos) * 0.5f / hpA0;
            const float hpa1 = -2.0f * hpCos / hpA0;
            const float hpa2 = (1.0f - hpAlpha) / hpA0;

            const float hpOut = hpb0 * wet + hpb1 * hpX1_ + hpb2 * hpX2_ - hpa1 * hpY1_
                - hpa2 * hpY2_;
            hpX2_ = hpX1_;
            hpX1_ = wet;
            hpY2_ = hpY1_;
            hpY1_ = hpOut;
            shaped = hpOut;
        }
        if (highcutSm_ < 0.999f) {
            const float lpFc = 4000.0f * std::pow(5.0f, highcutSm_);
            const float lpW0 = kTwoPi * lpFc / fs_;
            const float lpCos = std::cos(lpW0);
            const float lpAlpha = std::sin(lpW0) / std::sqrt(2.0f);
            const float lpA0 = 1.0f + lpAlpha;
            const float lpb0 = (1.0f - lpCos) * 0.5f / lpA0;
            const float lpb1 = (1.0f - lpCos) / lpA0;
            const float lpb2 = lpb0;
            const float lpa1 = -2.0f * lpCos / lpA0;
            const float lpa2 = (1.0f - lpAlpha) / lpA0;

            const float lpOut = lpb0 * shaped + lpb1 * lpX1_ + lpb2 * lpX2_ - lpa1 * lpY1_
                - lpa2 * lpY2_;
            lpX2_ = lpX1_;
            lpX1_ = shaped;
            lpY2_ = lpY1_;
            lpY1_ = lpOut;
            shaped = lpOut;
        }

        outL[i] = shaped * gainLin;
    }
}

void CabIrModule::reset()
{
    if (usePartitioned_) {
        partitioned_.reset();
        std::fill(blockWet_.begin(), blockWet_.end(), 0.0f);
    } else {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        bufPos_ = 0;
    }
    hpX1_ = 0.0f;
    hpX2_ = 0.0f;
    hpY1_ = 0.0f;
    hpY2_ = 0.0f;
    lpX1_ = 0.0f;
    lpX2_ = 0.0f;
    lpY1_ = 0.0f;
    lpY2_ = 0.0f;
}

void CabIrModule::setSampleRate(double)
{
    // prepare() is the authoritative entry point; Chain::prepare calls it
    // with the real max block before touching setSampleRate/setMaxBlock
}

void CabIrModule::setMaxBlock(int)
{
}

void CabIrModule::setParameter(const std::string& id, float value)
{
    if (id == "gain") {
        gain_ = value;
    } else if (id == "lowcut") {
        lowcut_ = value;
    } else if (id == "highcut") {
        highcut_ = value;
    }
}

} // namespace namfx
