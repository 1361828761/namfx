#pragma once

#include "desktop/Engine/engine_host.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace namfx {
namespace desktop {

// Minimal desktop editor shell (M5a first slice): audio I/O via JUCE, the
// engine host in the audio callback, and a tiny control bar (preset list,
// load button, tuner readout). UI surface grows in the next slices.
class NAMEditorApplication : public juce::JUCEApplication,
                             private juce::AudioAppComponent,
                             private juce::Timer {
public:
    NAMEditorApplication();

    const juce::String getApplicationName() override { return "NAM Editor"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override;
    void shutdown() override;
    void systemRequestedQuit() override;

    // AudioAppComponent
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // Timer: poll the tuner readout + scene/chain state
    void timerCallback() override;

private:
    void rebuildPresetList();
    void loadSelectedPreset();
    void buildUi();

    EngineHost host_;
    std::unique_ptr<juce::ComboBox> presetBox_;
    std::unique_ptr<juce::TextButton> loadButton_;
    std::unique_ptr<juce::Label> statusLabel_;
    std::unique_ptr<juce::Label> tunerLabel_;
    std::unique_ptr<juce::Label> sceneLabel_;
    juce::File demoDir_;
};

} // namespace desktop
} // namespace namfx
