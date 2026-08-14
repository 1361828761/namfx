#pragma once

#include "desktop/App/theme.h"
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

class NAMEditorApplication;

// Scrollable chain-edit surface: hosts the per-slot rows and shows a drop
// line while a reorder drag is in progress
class ChainPanelContent : public juce::Component {
public:
    void paint(juce::Graphics&) override;
    // row boundaries (end y of each module block), filled by the panel
    // rebuild so drop-position mapping is per-block, not per-child
    void setRowBounds(const std::vector<int>& bounds) { rowBounds_ = bounds; }
    void setDropIndexAt(int y);
    int takeDropIndex();
    int dropIndex() const { return dropIndex_; }

private:
    std::vector<int> rowBounds_;
    int dropIndex_ = -1;
};

// Drag grip on a chain row: drag up/down reorders the module (self-managed
// drag, no JUCE DragAndDropContainer dependency)
class GripLabel : public juce::Label {
public:
    GripLabel(int slot, NAMEditorApplication& app, ChainPanelContent& content);
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    int slot_;
    NAMEditorApplication& app_;
    ChainPanelContent& content_;
    bool dragging_ = false;
};
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

    // chain drag reorder callback (used by GripLabel / ChainPanelContent)
    void reorderChainByDrag(int srcSlot, int dstIndex);

    // Timer: poll the tuner readout + scene/chain state
    void timerCallback() override;

private:
    void rebuildPresetList();
    void loadSelectedPreset();
    void loadPresetFile(const juce::File& file);
    void buildUi();
    void refreshAudioDeviceControls();
    void applyAudioSetup();
    void updateTunerReadout();
    void rebuildChainPanel();
    void refreshChainViews();
    void rebuildAddModuleControls();
    void refreshModuleList();
    void refreshAssetList();
    void refreshModelLibrary();
    void addSectionLabel(const juce::String& text, int y);
    void saveUserPreset();

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
    NAMTheme theme_;
    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer player_;
    std::unique_ptr<EngineAudioSource> engineSource_;
    std::unique_ptr<juce::ComboBox> presetBox_;
    std::unique_ptr<juce::TextButton> loadButton_;
    std::unique_ptr<juce::Label> statusLabel_;
    std::unique_ptr<juce::Label> xrunLabel_; // under/overrun counter
    std::unique_ptr<juce::TextEditor> presetNameBox_;
    std::unique_ptr<juce::TextButton> savePresetButton_;
    std::vector<juce::File> presetFiles_; // demo + user presets (combo ids)
    // chain editing (M5c)
    std::unique_ptr<juce::ComboBox> addGroupBox_;   // module category
    std::unique_ptr<juce::ComboBox> addModuleBox_;  // module within category
    std::unique_ptr<juce::ComboBox> addAssetBox_;   // NAM model / IR library
    std::unique_ptr<juce::TextButton> addModuleButton_;
    std::unique_ptr<juce::Viewport> chainPanelViewport_;
    std::unique_ptr<ChainPanelContent> chainPanelContent_;
    // output panel
    std::unique_ptr<juce::Slider> masterSlider_;
    std::unique_ptr<juce::Slider> inputGainSlider_;
    std::unique_ptr<juce::Slider> bassSlider_;
    std::unique_ptr<juce::Slider> midSlider_;
    std::unique_ptr<juce::Slider> trebleSlider_;
    std::unique_ptr<juce::ToggleButton> muteToggle_;
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
    juce::File userPresetDir_;
    std::vector<juce::File> modelFiles_; // scanned NAM model library
    int tuning_ = 0;
    bool tunerOn_ = true;
    juce::uint32 pendingUnmute_ = 0; // device switch: unmute after warm-up
    int aliveTicks_ = 0; // crash-diagnostic heartbeat counter
};

} // namespace desktop
} // namespace namfx
