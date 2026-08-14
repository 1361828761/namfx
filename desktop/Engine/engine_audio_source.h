#pragma once

#include "desktop/Engine/engine_host.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <vector>

namespace namfx {
namespace desktop {

// JUCE AudioSource that drives the engine host. Channel-safe: mono devices
// deliver a 1-channel buffer; the missing channel is zero-fed and the
// processed result is collapsed back into the single output channel. Scratch
// buffers are sized in prepareToPlay, never inside the callback.
class EngineAudioSource : public juce::AudioSource {
public:
    explicit EngineAudioSource(EngineHost& host);

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

private:
    EngineHost& host_;
    std::vector<float> zeroIn_;
    std::vector<float> scratchL_;
    std::vector<float> scratchR_;
};

} // namespace desktop
} // namespace namfx
