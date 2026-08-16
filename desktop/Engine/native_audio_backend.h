#pragma once

#include "desktop/Engine/engine_audio_source.h"
#include "webui/server/audio_backend.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <mutex>

namespace namfx {
namespace desktop {

class NativeAudioBackend final : public web::AudioBackend {
public:
    explicit NativeAudioBackend(EngineHost& host);
    ~NativeAudioBackend() override;

    bool initialize(std::string& error) override;
    void shutdown() override;
    web::AudioBackendState state() const override;
    bool apply(const std::string& type, const std::string& device,
               double sampleRate, int blockSize, std::string& error) override;
    void tick() override;

private:
    void refreshStateLocked(web::AudioBackendState& out);
    void applyPendingUnmuteLocked();

    EngineHost& host_;
    // AudioDeviceManager is message-thread bound by JUCE's design; the
    // WebHost calls this class from its HTTP thread, so every entry point
    // serializes through this mutex (JUCE calls still happen off the
    // message thread, but the races they used to have are gone).
    mutable std::mutex mu_;
    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer player_;
    std::unique_ptr<EngineAudioSource> source_;
    juce::uint32 pendingUnmute_ = 0;
    bool initialized_ = false;
    std::string lastError_;
};

} // namespace desktop
} // namespace namfx
