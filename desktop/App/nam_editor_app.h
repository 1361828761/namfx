#pragma once

#include "desktop/App/tuner_meter.h"
#include "desktop/Engine/engine_audio_source.h"
#include "desktop/Engine/engine_host.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace namfx {
namespace desktop {

// Top-level window that hosts the editor content.
class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(juce::Component& content);

    void closeButtonPressed() override;
};

// Desktop editor shell (M5a): the engine host in the audio callback, a
// preset control bar, a chain view, an audio-device panel (device type /
// device / sample rate / buffer size -> latency control) and a per-string
// tuner readout. The audio path is an AudioDeviceManager + AudioSourcePlayer
// + EngineAudioSource so the buffer size (and with it the round-trip
// latency) is user-controllable.
class NAMEditorApplication : public juce::JUCEApplication,
                             private juce::Timer,
                             public juce::Component {
public:
    NAMEditorApplication();

    const juce::String getApplicationName() override { return "NAM Editor"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override;
    void shutdown() override;
    void systemRequestedQuit() override;

    // Component: content pane of the MainWindow
    void paint(juce::Graphics&) override;

    // Timer: poll the tuner readout + scene/chain state
    void timerCallback() override;

private:
    void rebuildPresetList();
    void loadSelectedPreset();
    void buildUi();
    void refreshAudioDeviceControls();
    void applyAudioSetup();
    void updateTunerReadout();

    // tunings: index 0 = EADGBE (E2 A2 D3 G3 B3 E4), 1 = Drop D (D2...)
    static constexpr int kTuningCount = 2;
    static constexpr int kStringCount = 6;
    static constexpr int kTargetNotes[kTuningCount][kStringCount] = {
        {40, 45, 50, 55, 59, 64},
        {38, 45, 50, 55, 59, 64},
    };
    static const char* kTuningNames[kTuningCount];

    EngineHost host_;
    std::unique_ptr<MainWindow> mainWindow_;
    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer player_;
    std::unique_ptr<EngineAudioSource> engineSource_;
    std::unique_ptr<juce::ComboBox> presetBox_;
    std::unique_ptr<juce::TextButton> loadButton_;
    std::unique_ptr<juce::Label> statusLabel_;
    std::unique_ptr<juce::TextEditor> chainLabel_;
    // audio device panel
    std::unique_ptr<juce::ComboBox> deviceTypeBox_;
    std::unique_ptr<juce::ComboBox> deviceBox_;
    std::unique_ptr<juce::ComboBox> sampleRateBox_;
    std::unique_ptr<juce::ComboBox> bufferSizeBox_;
    std::unique_ptr<juce::TextButton> applyAudioButton_;
    // tuner panel
    std::unique_ptr<juce::ComboBox> tuningBox_; // EADGBE / Drop D
    std::unique_ptr<TunerMeter> tunerMeter_;
    std::unique_ptr<juce::Label> tunerLabel_;
    std::unique_ptr<juce::Label> sceneLabel_;
    juce::File demoDir_;
    int tuning_ = 0;
    int aliveTicks_ = 0; // crash-diagnostic heartbeat counter
};

} // namespace desktop
} // namespace namfx
