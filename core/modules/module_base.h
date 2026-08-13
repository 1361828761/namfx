#pragma once

namespace namfx {

enum class ChannelMode {
    MonoInMonoOut,
    MonoInStereoOut,
    StereoInStereoOut,
};

class ModuleBase {
public:
    virtual ~ModuleBase() = default;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void process(const float* inL, const float* inR, float* outL, float* outR, int n) = 0;
    virtual void reset() = 0;
    virtual void setSampleRate(double sampleRate) = 0;
    virtual void setMaxBlock(int maxBlockSize) = 0;
    virtual ChannelMode channelMode() const = 0;
};

} // namespace namfx
