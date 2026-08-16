#include "modules/nam/nam_amp.h"

#include "modules/ir/resample_stream.h"
#include "modules/module_registry.h"

#include "dsp.h"
#include "get_dsp.h"
#include "slimmable.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

namespace {

// 0..1 -> -12..+12 dB linear mapping (0.5 = 0 dB), same convention as cab.ir
float dbFromParam(float p)
{
    return -12.0f + 24.0f * p;
}

} // namespace

struct NamAmpModule::Impl {
    std::unique_ptr<nam::DSP> dsp;
    double modelRate = 48000.0;
    double engineRate = 48000.0;
    bool prepared = false;
    bool slimmable = false;
    float tier = 1.0f;
    float tierApplied = 1.0f;

    bool resampleIn = false;
    ir::StreamingResampler inRes;
    ir::StreamingResampler outRes;
    int dspBlockMax = 1;
    std::vector<float> inBuf;
    std::vector<float> dspIn;
    std::vector<float> dspOut;
    std::vector<float> resOut;
    std::vector<float> outQueue;
    std::size_t outQRead = 0;
    std::size_t outQWrite = 0;

    float smoothK = 0.0f;
    float gain = 0.5f;
    float gainSm = 0.5f;
    float bass = 0.5f;
    float bassSm = 0.5f;
    float middle = 0.5f;
    float middleSm = 0.5f;
    float treble = 0.5f;
    float trebleSm = 0.5f;
    float output = 0.5f;
    float outputSm = 0.5f;

    // post EQ (RBJ biquads, independent state each): low shelf 250 Hz,
    // peaking 1 kHz, high shelf 4 kHz, run in the engine-rate domain
    float bB0 = 1.0f, bB1 = 0.0f, bB2 = 0.0f, bA1 = 0.0f, bA2 = 0.0f;
    float bX1 = 0.0f, bX2 = 0.0f, bY1 = 0.0f, bY2 = 0.0f;
    float mB0 = 1.0f, mB1 = 0.0f, mB2 = 0.0f, mA1 = 0.0f, mA2 = 0.0f;
    float mX1 = 0.0f, mX2 = 0.0f, mY1 = 0.0f, mY2 = 0.0f;
    float tB0 = 1.0f, tB1 = 0.0f, tB2 = 0.0f, tA1 = 0.0f, tA2 = 0.0f;
    float tX1 = 0.0f, tX2 = 0.0f, tY1 = 0.0f, tY2 = 0.0f;

    void updateEqCoeffs(float fs)
    {
        constexpr float kPi = 3.14159265358979323846f;
        // Q = 1/sqrt(2) for all three bands -> alpha = sin(w0)/sqrt(2)
        const float alphaScale = 1.0f / std::sqrt(2.0f);

        auto shelf = [&](float f0, float db, float& b0, float& b1, float& b2, float& a1,
                         float& a2) {
            const float A = std::pow(10.0f, db / 40.0f);
            const float w0 = 2.0f * kPi * f0 / fs;
            const float cw = std::cos(w0);
            const float alpha = std::sin(w0) * alphaScale; // S = 1
            const float a0 = (A + 1.0f) + (A - 1.0f) * cw + 2.0f * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cw + 2.0f * std::sqrt(A) * alpha) / a0;
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw) / a0;
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cw - 2.0f * std::sqrt(A) * alpha) / a0;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw) / a0;
            a2 = ((A + 1.0f) + (A - 1.0f) * cw - 2.0f * std::sqrt(A) * alpha) / a0;
        };
        auto peak = [&](float f0, float db, float& b0, float& b1, float& b2, float& a1,
                        float& a2) {
            const float A = std::pow(10.0f, db / 40.0f);
            const float w0 = 2.0f * kPi * f0 / fs;
            const float cw = std::cos(w0);
            const float alpha = std::sin(w0) * alphaScale; // Q = 1/sqrt(2)
            const float a0 = 1.0f + alpha / A;
            b0 = (1.0f + alpha * A) / a0;
            b1 = -2.0f * cw / a0;
            b2 = (1.0f - alpha * A) / a0;
            a1 = -2.0f * cw / a0;
            a2 = (1.0f - alpha / A) / a0;
        };

        const float bassDb = dbFromParam(bassSm);
        const float midDb = dbFromParam(middleSm);
        const float trebDb = dbFromParam(trebleSm);
        shelf(250.0f, bassDb, bB0, bB1, bB2, bA1, bA2);
        peak(1000.0f, midDb, mB0, mB1, mB2, mA1, mA2);
        shelf(4000.0f, trebDb, tB0, tB1, tB2, tA1, tA2);
    }
};

NamAmpModule::~NamAmpModule() = default;

bool NamAmpModule::loadAsset(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::ostringstream text;
    text << stream.rdbuf();
    const std::string bytes = text.str();
    return loadAssetBytes(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

bool NamAmpModule::loadAssetBytes(const std::uint8_t* data, std::size_t size)
{
    try {
        impl_ = std::make_unique<Impl>();
        // .nam files are JSON; the json overload is pure memory (no fs)
        const nlohmann::json config = nlohmann::json::parse(
            reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + size);
        impl_->dsp = nam::get_dsp(config);
        const double expected = impl_->dsp->GetExpectedSampleRate();
        impl_->modelRate = expected > 0.0 ? expected : 48000.0;
        impl_->slimmable = dynamic_cast<nam::SlimmableModel*>(impl_->dsp.get()) != nullptr;
        return true;
    } catch (const std::exception&) {
        impl_.reset();
        return false;
    }
}

void NamAmpModule::applyTier()
{
    if (!impl_ || !impl_->slimmable || !impl_->dsp) {
        return;
    }
    if (std::fabs(impl_->tier - impl_->tierApplied) < 1e-6f) {
        return;
    }
    // NAM Core's SetSlimmableSize locks a mutex and resets the active
    // submodel (with prewarm): control thread only
    auto* slim = dynamic_cast<nam::SlimmableModel*>(impl_->dsp.get());
    slim->SetSlimmableSize(impl_->tier);
    impl_->tierApplied = impl_->tier;
}

void NamAmpModule::prepare(double sampleRate, int maxBlockSize)
{
    if (!impl_ || !impl_->dsp) {
        return;
    }
    const int maxBlock = std::max(maxBlockSize, 1);
    impl_->smoothK = 1.0f - std::exp(-1.0f / (0.01f * static_cast<float>(sampleRate)));
    impl_->gainSm = impl_->gain;
    impl_->bassSm = impl_->bass;
    impl_->middleSm = impl_->middle;
    impl_->trebleSm = impl_->treble;
    impl_->outputSm = impl_->output;

    impl_->engineRate = sampleRate;
    impl_->resampleIn = std::fabs(impl_->engineRate - impl_->modelRate) > 1.0;
    impl_->dspBlockMax = static_cast<int>(
        std::ceil(static_cast<double>(maxBlock) * impl_->modelRate / impl_->engineRate)) + 64;
    if (impl_->resampleIn) {
        impl_->inRes.prepare(impl_->engineRate, impl_->modelRate, maxBlock);
        impl_->outRes.prepare(impl_->modelRate, impl_->engineRate, impl_->dspBlockMax);
    }
    impl_->inBuf.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    impl_->dspIn.assign(static_cast<std::size_t>(impl_->dspBlockMax), 0.0f);
    impl_->dspOut.assign(static_cast<std::size_t>(impl_->dspBlockMax), 0.0f);
    const int resCap = impl_->resampleIn
        ? impl_->outRes.outCapacity(impl_->dspBlockMax) + maxBlock + 1
        : maxBlock;
    impl_->resOut.assign(static_cast<std::size_t>(resCap), 0.0f);
    impl_->outQueue.assign(static_cast<std::size_t>(resCap + maxBlock + 1), 0.0f);
    impl_->outQRead = 0;
    impl_->outQWrite = 0;

    impl_->dsp->Reset(impl_->modelRate, impl_->dspBlockMax);
    applyTier(); // control thread; restores the tier target (idempotent)
    impl_->updateEqCoeffs(static_cast<float>(impl_->engineRate));
    impl_->bX1 = impl_->bX2 = impl_->bY1 = impl_->bY2 = 0.0f;
    impl_->mX1 = impl_->mX2 = impl_->mY1 = impl_->mY2 = 0.0f;
    impl_->tX1 = impl_->tX2 = impl_->tY1 = impl_->tY2 = 0.0f;
    impl_->prepared = true;
}

void NamAmpModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!impl_ || !impl_->prepared) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    Impl& im = *impl_;

    // input gain (pre, before the model's nonlinearity), smoothed per sample
    for (int i = 0; i < n; ++i) {
        im.gainSm += im.smoothK * (im.gain - im.gainSm);
        const float gainLin = std::pow(10.0f, dbFromParam(im.gainSm) / 20.0f);
        im.inBuf[static_cast<std::size_t>(i)] = inL[i] * gainLin;
    }

    int m = 0;
    if (im.resampleIn) {
        m = im.inRes.process(im.inBuf.data(), n, im.dspIn.data());
    } else {
        m = n;
        std::memcpy(im.dspIn.data(), im.inBuf.data(), static_cast<std::size_t>(n) * sizeof(float));
    }
    if (m > 0) {
        float* inPtrs[1] = { im.dspIn.data() };
        float* outPtrs[1] = { im.dspOut.data() };
        im.dsp->process(inPtrs, outPtrs, m);
    }

    // output into the queue (resampled back when the engine rate differs)
    int produced = 0;
    if (im.resampleIn) {
        produced = im.outRes.process(im.dspOut.data(), m, im.resOut.data());
    } else {
        produced = m;
        std::memcpy(im.resOut.data(), im.dspOut.data(), static_cast<std::size_t>(m) * sizeof(float));
    }
    for (int i = 0; i < produced; ++i) {
        im.outQueue[im.outQWrite++] = im.resOut[static_cast<std::size_t>(i)];
    }

    // consume n samples (zero-pad while the resampler is still warming up);
    // tone params smooth per sample, EQ coefficients refresh per block
    im.updateEqCoeffs(static_cast<float>(im.engineRate));
    for (int i = 0; i < n; ++i) {
        im.bassSm += im.smoothK * (im.bass - im.bassSm);
        im.middleSm += im.smoothK * (im.middle - im.middleSm);
        im.trebleSm += im.smoothK * (im.treble - im.trebleSm);
        im.outputSm += im.smoothK * (im.output - im.outputSm);
        const float outLin = std::pow(10.0f, dbFromParam(im.outputSm) / 20.0f);
        float v = 0.0f;
        if (im.outQRead < im.outQWrite) {
            v = im.outQueue[im.outQRead++];
        }
        // post EQ: treble -> middle -> bass cascade, engine-rate domain
        const float ty = im.tB0 * v + im.tB1 * im.tX1 + im.tB2 * im.tX2 - im.tA1 * im.tY1
            - im.tA2 * im.tY2;
        im.tX2 = im.tX1;
        im.tX1 = v;
        im.tY2 = im.tY1;
        im.tY1 = ty;
        const float my = im.mB0 * ty + im.mB1 * im.mX1 + im.mB2 * im.mX2 - im.mA1 * im.mY1
            - im.mA2 * im.mY2;
        im.mX2 = im.mX1;
        im.mX1 = ty;
        im.mY2 = im.mY1;
        im.mY1 = my;
        const float by = im.bB0 * my + im.bB1 * im.bX1 + im.bB2 * im.bX2 - im.bA1 * im.bY1
            - im.bA2 * im.bY2;
        im.bX2 = im.bX1;
        im.bX1 = my;
        im.bY2 = im.bY1;
        im.bY1 = by;
        outL[i] = by * outLin;
    }

    // compact the queue occasionally to bound memory positions
    if (im.outQRead >= im.outQueue.size() / 2 && im.outQRead > 0) {
        const std::size_t remain = im.outQWrite - im.outQRead;
        for (std::size_t i = 0; i < remain; ++i) {
            im.outQueue[i] = im.outQueue[im.outQRead + i];
        }
        im.outQRead = 0;
        im.outQWrite = remain;
    }
}

void NamAmpModule::reset()
{
    if (!impl_ || !impl_->dsp) {
        return;
    }
    impl_->inRes.reset();
    impl_->outRes.reset();
    impl_->outQRead = 0;
    impl_->outQWrite = 0;
    impl_->dsp->Reset(impl_->modelRate, impl_->dspBlockMax);
    impl_->bX1 = impl_->bX2 = impl_->bY1 = impl_->bY2 = 0.0f;
    impl_->mX1 = impl_->mX2 = impl_->mY1 = impl_->mY2 = 0.0f;
    impl_->tX1 = impl_->tX2 = impl_->tY1 = impl_->tY2 = 0.0f;
    impl_->gainSm = impl_->gain;
    impl_->bassSm = impl_->bass;
    impl_->middleSm = impl_->middle;
    impl_->trebleSm = impl_->treble;
    impl_->outputSm = impl_->output;
    applyTier(); // control thread: restore the tier target after a reload
}

void NamAmpModule::setSampleRate(double)
{
    // prepare() is the authoritative entry point; Chain::prepare calls it
    // with the real max block before touching setSampleRate/setMaxBlock
}

void NamAmpModule::setMaxBlock(int)
{
}

void NamAmpModule::setParameter(const std::string& id, float value)
{
    if (!impl_) {
        return;
    }
    if (id == "gain") {
        impl_->gain = value;
    } else if (id == "bass") {
        impl_->bass = value;
    } else if (id == "middle") {
        impl_->middle = value;
    } else if (id == "treble") {
        impl_->treble = value;
    } else if (id == "output") {
        impl_->output = value;
    } else if (id == "tier") {
        // audio-callback thread only records the target; applyTier() (a
        // control-thread call) performs the actual slimmable switch
        impl_->tier = value;
    }
}

void registerNamAmp(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"gain", "Gain", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"bass", "Bass", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"middle", "Middle", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"treble", "Treble", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"output", "Output", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"tier", "Tier", 0.0f, 1.0f, 1.0f, "", Taper::Linear});
    registry.registerModule("amp.nam", "amp", std::move(specs),
                            [] { return std::make_unique<NamAmpModule>(); });
}

} // namespace namfx
