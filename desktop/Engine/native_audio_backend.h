#pragma once

#include "desktop/Engine/engine_audio_source.h"
#include "webui/server/audio_backend.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace namfx {
namespace desktop {

class NativeAudioBackend final : public web::AudioBackend, private juce::Timer {
public:
    explicit NativeAudioBackend(EngineHost& host);
    ~NativeAudioBackend() override;

    bool initialize(std::string& error) override;
    void shutdown() override;
    web::AudioBackendState state() const override;
    bool apply(const std::string& type, const std::string& device,
               double sampleRate, int blockSize, std::string& error) override;

private:
    void timerCallback() override;
    void refreshState(web::AudioBackendState& out) const;

    EngineHost& host_;
    mutable juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer player_;
    std::unique_ptr<EngineAudioSource> source_;
    juce::uint32 pendingUnmute_ = 0;
    bool initialized_ = false;
    mutable std::string lastError_;
};

} // namespace desktop
} // namespace namfx
