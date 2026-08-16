#include "desktop/Engine/native_audio_backend.h"

#include <algorithm>

namespace namfx {
namespace desktop {

NativeAudioBackend::NativeAudioBackend(EngineHost& host) : host_(host) {}

NativeAudioBackend::~NativeAudioBackend()
{
    shutdown();
}

bool NativeAudioBackend::initialize(std::string& error)
{
    std::lock_guard<std::mutex> lock(mu_);
    if (initialized_) {
        return true;
    }

    source_ = std::make_unique<EngineAudioSource>(host_);
    player_.setSource(source_.get());
    host_.output().setMute(true);

    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.sampleRate = 44100.0;
    preferred.bufferSize = 128;
    juce::String result = deviceManager_.initialise(2, 2, nullptr, true, {}, &preferred);
    if (result.isNotEmpty()) {
        lastError_ = result.toStdString();
    }

    deviceManager_.addAudioCallback(&player_);
    initialized_ = true;

    // Prefer ASIO when a driver is installed; otherwise keep JUCE's default.
    const auto& types = deviceManager_.getAvailableDeviceTypes();
    for (int i = 0; i < types.size(); ++i) {
        if (types[i]->getTypeName().equalsIgnoreCase("ASIO")) {
            deviceManager_.setCurrentAudioDeviceType(types[i]->getTypeName(), false);
            auto* type = deviceManager_.getCurrentDeviceTypeObject();
            if (type != nullptr) {
                // true = probe which devices can actually be opened; this
                // runs once at startup, not on every state snapshot
                const juce::StringArray names = type->getDeviceNames(true);
                if (!names.isEmpty()) {
                    juce::AudioDeviceManager::AudioDeviceSetup setup;
                    deviceManager_.getAudioDeviceSetup(setup);
                    setup.inputDeviceName = names[0];
                    setup.outputDeviceName = names[0];
                    setup.sampleRate = 44100.0;
                    setup.bufferSize = 128;
                    const juce::String openError = deviceManager_.setAudioDeviceSetup(setup, true);
                    if (openError.isNotEmpty()) {
                        lastError_ = openError.toStdString();
                    }
                }
            }
            break;
        }
    }

    web::AudioBackendState current;
    refreshStateLocked(current);
    if (!current.active && lastError_.empty()) {
        lastError_ = "没有可用的音频设备";
    }
    error = lastError_;
    // delayed un-mute after the device warms up; the host ticker drives
    // applyPendingUnmuteLocked() every 100 ms
    pendingUnmute_ = juce::Time::getMillisecondCounter() + 1500;
    return current.active;
}

void NativeAudioBackend::shutdown()
{
    std::lock_guard<std::mutex> lock(mu_);
    pendingUnmute_ = 0;
    if (!initialized_) {
        return;
    }
    deviceManager_.removeAudioCallback(&player_);
    player_.setSource(nullptr);
    deviceManager_.closeAudioDevice();
    source_.reset();
    initialized_ = false;
}

web::AudioBackendState NativeAudioBackend::state() const
{
    // JUCE's device manager API is non-const by design; the backend
    // interface is const. All state is mutex-guarded, so the const_cast
    // is safe and only undoes interface constness.
    auto* self = const_cast<NativeAudioBackend*>(this);
    std::lock_guard<std::mutex> lock(self->mu_);
    self->applyPendingUnmuteLocked();
    web::AudioBackendState out;
    self->refreshStateLocked(out);
    return out;
}

void NativeAudioBackend::refreshStateLocked(web::AudioBackendState& out)
{
    const auto& types = deviceManager_.getAvailableDeviceTypes();
    const juce::String currentType = deviceManager_.getCurrentAudioDeviceType();
    for (int i = 0; i < types.size(); ++i) {
        web::AudioDeviceTypeInfo info;
        info.name = types[i]->getTypeName().toStdString();
        // false = list device names without opening each one (snapshot path)
        const juce::StringArray names = types[i]->getDeviceNames(false);
        for (const juce::String& name : names) {
            info.devices.push_back(name.toStdString());
        }
        out.types.push_back(std::move(info));
    }
    out.type = currentType.toStdString();
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        out.device = device->getName().toStdString();
        out.sampleRate = device->getCurrentSampleRate();
        out.blockSize = device->getCurrentBufferSizeSamples();
        out.active = true;
    }
    out.error = lastError_;
}

bool NativeAudioBackend::apply(const std::string& type, const std::string& device,
                               double sampleRate, int blockSize, std::string& error)
{
    std::lock_guard<std::mutex> lock(mu_);
    if (!initialized_) {
        error = "音频后端未初始化";
        return false;
    }
    host_.output().setMute(true);
    deviceManager_.setCurrentAudioDeviceType(juce::String(type), false);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.inputDeviceName = juce::String(device);
    setup.outputDeviceName = juce::String(device);
    setup.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    setup.bufferSize = blockSize > 0 ? blockSize : 128;
    const juce::String result = deviceManager_.setAudioDeviceSetup(setup, true);
    host_.output().reset();
    if (result.isNotEmpty()) {
        lastError_ = result.toStdString();
        error = lastError_;
        pendingUnmute_ = juce::Time::getMillisecondCounter() + 300;
        return false;
    }
    lastError_.clear();
    // never touch JUCE timers from the HTTP thread: record the deadline and
    // let the host ticker perform the un-mute
    pendingUnmute_ = juce::Time::getMillisecondCounter() + 1500;
    error.clear();
    return true;
}

void NativeAudioBackend::tick()
{
    std::lock_guard<std::mutex> lock(mu_);
    applyPendingUnmuteLocked();
}

void NativeAudioBackend::applyPendingUnmuteLocked()
{
    if (pendingUnmute_ != 0 && juce::Time::getMillisecondCounter() >= pendingUnmute_) {
        pendingUnmute_ = 0;
        host_.output().setMute(false);
    }
}

} // namespace desktop
} // namespace namfx
