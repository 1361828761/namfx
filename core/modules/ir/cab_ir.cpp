#include "modules/ir/cab_ir.h"

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

void CabIrModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;

    if (assetLoaded_) {
        ir_ = ir::resampleLinear(rawIr_, rawRate_, sampleRate);
    } else {
        ir_.clear();
    }
    if (ir_.empty()) {
        ir_.assign(1, 1.0f); // degenerate unit IR keeps the chain running
    }
    buf_.assign(ir_.size(), 0.0f);

    reset();
    gainSm_ = gain_;
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
    for (int i = 0; i < n; ++i) {
        gainSm_ += smoothK_ * (gain_ - gainSm_);
        // gain 0..1 -> -12..+12 dB (0.5 = 0 dB neutral)
        const float gainLin = std::pow(10.0f, (-12.0f + 24.0f * gainSm_) / 20.0f);

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
        outL[i] = acc * gainLin;
    }
}

void CabIrModule::reset()
{
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    bufPos_ = 0;
}

void CabIrModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void CabIrModule::setMaxBlock(int)
{
}

void CabIrModule::setParameter(const std::string& id, float value)
{
    if (id == "gain") {
        gain_ = value;
    }
}

} // namespace namfx
