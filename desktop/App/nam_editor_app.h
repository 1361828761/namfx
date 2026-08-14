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

    // tunings, indexed 1st string (high E) .. 6th string (low E):
    // 0 = EADGBE (E4 B3 G3 D3 A2 E2), 1 = Drop D (E4 B3 G3 D3 A2 D2)
    static constexpr int kTuningCount = 2;
    static constexpr int kStringCount = 6;
    static constexpr int kTargetNotes[kTuningCount][kStringCount] = {
        {64, 59, 55, 50, 45, 40},
        {64, 59, 55, 50, 45, 38},
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
    std::unique_ptr<juce::Label> xrunLabel_; // under/overrun counter
    std::unique_ptr<juce::TextEditor> chainLabel_;
    // audio device panel
    std::unique_ptr<juce::ComboBox> deviceTypeBox_;
    std::unique_ptr<juce::ComboBox> deviceBox_;
    std::unique_ptr<juce::ComboBox> sampleRateBox_;
    std::unique_ptr<juce::ComboBox> bufferSizeBox_;
    std::unique_ptr<juce::TextButton> applyAudioButton_;
    // tuner panel
    std::unique_ptr<juce::ComboBox> tuningBox_; // EADGBE / Drop D
    std::unique_ptr<juce::ToggleButton> tunerToggle_;
    std::unique_ptr<juce::ToggleButton> bypassToggle_; // master bypass
    std::unique_ptr<TunerMeter> tunerMeter_;
    std::unique_ptr<juce::Label> tunerLabel_;
    std::unique_ptr<juce::Label> sceneLabel_;
    juce::File demoDir_;
    int tuning_ = 0;
    bool tunerOn_ = true;
    juce::uint32 pendingUnmute_ = 0; // device switch: unmute after warm-up
    int aliveTicks_ = 0; // crash-diagnostic heartbeat counter
};

} // namespace desktop
} // namespace namfx
