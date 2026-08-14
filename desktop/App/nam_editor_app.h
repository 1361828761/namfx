#pragma once

#include "desktop/Engine/engine_host.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace namfx {
namespace desktop {

// Top-level window that hosts the editor content. AudioAppComponent is a
// plain Component; without a window it never becomes visible (the audio
// device still runs, which made the first slice feel "headless").
class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(juce::Component& content);

    void closeButtonPressed() override;
};

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

    // AudioAppComponent is a private base; expose the component view for
    // the host window (the upcast happens inside the class)
    juce::Component& asComponent() { return *this; }

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
    std::unique_ptr<MainWindow> mainWindow_;
    std::unique_ptr<juce::ComboBox> presetBox_;
    std::unique_ptr<juce::TextButton> loadButton_;
    std::unique_ptr<juce::Label> statusLabel_;
    std::unique_ptr<juce::Label> tunerLabel_;
    std::unique_ptr<juce::Label> sceneLabel_;
    juce::File demoDir_;
    // audio-callback scratch (prepared once; zero allocation inside the
    // callback): mono devices only deliver one channel, so the missing
    // channel is zero-fed and the processed result is collapsed back
    std::vector<float> zeroIn_;
    std::vector<float> scratchL_;
    std::vector<float> scratchR_;
};

} // namespace desktop
} // namespace namfx
