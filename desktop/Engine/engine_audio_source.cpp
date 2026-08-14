#include "desktop/Engine/engine_audio_source.h"

#include <algorithm>
#include <cstddef>

namespace namfx {
namespace desktop {

EngineAudioSource::EngineAudioSource(EngineHost& host) : host_(host)
{
}

void EngineAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    // scratch buffers are sized here (device/block changes), never inside
    // the callback; zeroed so a silent input can never leak stale data
    zeroIn_.assign(static_cast<std::size_t>(samplesPerBlockExpected), 0.0f);
    scratchL_.assign(static_cast<std::size_t>(samplesPerBlockExpected), 0.0f);
    scratchR_.assign(static_cast<std::size_t>(samplesPerBlockExpected), 0.0f);
    host_.prepare(sampleRate, samplesPerBlockExpected);
}

void EngineAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    const int n = bufferToFill.numSamples;
    juce::AudioBuffer<float>& buf = *bufferToFill.buffer;
    const int numCh = buf.getNumChannels();
    const float* inL = numCh > 0 ? buf.getReadPointer(0, bufferToFill.startSample)
                                 : zeroIn_.data();
    const float* inR = numCh > 1 ? buf.getReadPointer(1, bufferToFill.startSample)
                                 : zeroIn_.data();
    if (numCh > 1) {
        float* outL = buf.getWritePointer(0, bufferToFill.startSample);
        float* outR = buf.getWritePointer(1, bufferToFill.startSample);
        host_.process(inL, inR, outL, outR, n);
    } else if (numCh == 1) {
        float* outL = buf.getWritePointer(0, bufferToFill.startSample);
        host_.process(inL, zeroIn_.data(), scratchL_.data(), scratchR_.data(), n);
        for (int i = 0; i < n; ++i) {
            outL[i] = scratchL_[static_cast<std::size_t>(i)];
        }
    } else {
        host_.process(zeroIn_.data(), zeroIn_.data(), scratchL_.data(), scratchR_.data(), n);
    }
}

void EngineAudioSource::releaseResources()
{
}

} // namespace desktop
} // namespace namfx
