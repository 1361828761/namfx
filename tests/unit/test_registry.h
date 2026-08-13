#pragma once

#include "modules/dsp/gain.h"
#include "modules/dsp/tone.h"
#include "modules/dsp/ts808.h"
#include "modules/dsp/klon.h"
#include "modules/dsp/ocd.h"
#include "modules/dsp/ota_comp.h"
#include "modules/dsp/chorus.h"
#include "modules/dsp/flanger.h"
#include "modules/dsp/phaser.h"
#include "modules/dsp/wah.h"
#include "modules/dsp/ns2_gate.h"
#include "modules/dsp/ge7_eq.h"
#include "modules/dsp/dm2_delay.h"
#include "modules/dsp/tape_delay.h"
#include "modules/dsp/spring_reverb.h"
#include "modules/dsp/pitch_shifter.h"
#include "modules/module_base.h"
#include "modules/module_registry.h"

#include <memory>

namespace testx {

class StereoPassthroughModule final : public namfx::ModuleBase {
public:
    void prepare(double, int) override {}
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override
    {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
            outR[i] = inR[i];
        }
    }
    void reset() override {}
    void setSampleRate(double) override {}
    void setMaxBlock(int) override {}
    namfx::ChannelMode channelMode() const override
    {
        return namfx::ChannelMode::StereoInStereoOut;
    }
};

inline std::shared_ptr<const namfx::ModuleRegistry> makeRegistry()
{
    auto registry = std::make_shared<namfx::ModuleRegistry>();
    namfx::registerGain(*registry);
    namfx::registerTone(*registry);
    namfx::registerTs808(*registry);
    namfx::registerTransparent(*registry);
    namfx::registerMosfetOd(*registry);
    namfx::registerOtaComp(*registry);
    namfx::registerChorus(*registry);
    namfx::registerFlanger(*registry);
    namfx::registerPhaser(*registry);
    namfx::registerWah(*registry);
    namfx::registerNs2Gate(*registry);
    namfx::registerGe7Eq(*registry);
    namfx::registerDm2Delay(*registry);
    namfx::registerTapeDelay(*registry);
    namfx::registerSpringReverb(*registry);
    namfx::registerPitchShifter(*registry);
    registry->registerModule("stereo.passthrough", "pedal", {},
                             [] { return std::make_unique<StereoPassthroughModule>(); });
    return registry;
}

} // namespace testx
