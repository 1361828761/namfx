#include "desktop/App/nam_editor_app.h"

#include <juce_core/juce_core.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

// crash diagnostics (global scope): the headless test runs saw the app exit
// with code 3 (abort) and no WER record; log SEH/abort/lifecycle events to
// a file so the failure point is identifiable. Control-thread only.
void crashLog(const char* msg)
{
    FILE* f = std::fopen("namfx_crash.log", "a");
    if (f != nullptr) {
        std::fprintf(f, "%s\n", msg);
        std::fclose(f);
    }
}

#ifdef _WIN32
LONG WINAPI sehHandler(EXCEPTION_POINTERS* ep)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "SEH code=0x%08lX addr=%p",
                  static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),
                  ep->ExceptionRecord->ExceptionAddress);
    crashLog(buf);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void abortHandler(int)
{
#ifdef _WIN32
    void* frames[48];
    const USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);
    FILE* f = std::fopen("namfx_crash.log", "a");
    if (f != nullptr) {
        std::fprintf(f, "abort() stack (%u frames):\n", static_cast<unsigned>(count));
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        if (SymInitialize(GetCurrentProcess(), nullptr, TRUE)) {
            for (int i = 0; i < count; ++i) {
                char symBuf[sizeof(SYMBOL_INFO) + 256];
                auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
                sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                sym->MaxNameLen = 256;
                DWORD64 disp = 0;
                if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(frames[i]), &disp, sym)) {
                    std::fprintf(f, "  %02d 0x%llX %s+0x%llX\n", i,
                                 reinterpret_cast<unsigned long long>(frames[i]), sym->Name,
                                 static_cast<unsigned long long>(disp));
                } else {
                    std::fprintf(f, "  %02d 0x%llX (no sym)\n", i,
                                 reinterpret_cast<unsigned long long>(frames[i]));
                }
            }
            SymCleanup(GetCurrentProcess());
        }
        std::fclose(f);
    }
#else
    crashLog("abort()");
#endif
}

namespace namfx {
namespace desktop {

namespace {

constexpr int kTimerMs = 100;

juce::String noteName(int midi)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int octave = midi / 12 - 1;
    return juce::String(names[midi % 12]) + juce::String(octave);
}

} // namespace

const char* NAMEditorApplication::kTuningNames[kTuningCount] = {"EADGBE", "Drop D"};

MainWindow::MainWindow(juce::Component& content)
    : juce::DocumentWindow("NAM Editor", juce::Colours::darkgrey,
                           juce::DocumentWindow::allButtons)
{
    setContentNonOwned(&content, false);
    setResizable(true, false);
    setUsingNativeTitleBar(true);
    // remember the window size across launches (the user resizes it once,
    // it stays that way); default is large enough for the device + tuner +
    // chain panels
    juce::PropertiesFile::Options opts;
    opts.applicationName = "NAM Editor";
    opts.filenameSuffix = ".settings";
    opts.folderName = "namfx";
    juce::PropertiesFile props(opts);
    const int w = props.getIntValue("windowW", 1280);
    const int h = props.getIntValue("windowH", 720);
    content.setSize(w, h);
    centreWithSize(content.getWidth(), content.getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "NAM Editor";
    opts.filenameSuffix = ".settings";
    opts.folderName = "namfx";
    juce::PropertiesFile props(opts);
    if (auto* content = getContentComponent()) {
        props.setValue("windowW", content->getWidth());
        props.setValue("windowH", content->getHeight());
    }
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

NAMEditorApplication::NAMEditorApplication() = default;

void NAMEditorApplication::initialise(const juce::String&)
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(&sehHandler);
#endif
    std::signal(SIGABRT, &abortHandler);
    crashLog("lifecycle: initialise begin");
    demoDir_ = juce::File(NAMFX_DEMO_DIR);
    if (!demoDir_.isDirectory()) {
        // fall back to the working directory (dev launches from the repo root)
        demoDir_ = juce::File::getCurrentWorkingDirectory().getChildFile("core/preset/demo");
    }
    engineSource_ = std::make_unique<EngineAudioSource>(host_);
    player_.setSource(engineSource_.get());
    // start muted: opening the device can pop (interface power-up noise);
    // the timer fades the output in after the device has settled
    host_.output().setMute(true);
    // request a small buffer for low round-trip latency; the device panel
    // lets the user pick exclusive / low-latency WASAPI modes and smaller
    // buffer sizes afterwards
    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.bufferSize = 128;
    const juce::String err =
        deviceManager_.initialise(2, 2, nullptr, true, {}, &preferred);
    deviceManager_.addAudioCallback(&player_);
    buildUi();
    mainWindow_ = std::make_unique<MainWindow>(*this);
    refreshAudioDeviceControls();
    // diagnostics: which device types JUCE enumerated (ASIO present?)
    {
        const juce::OwnedArray<juce::AudioIODeviceType>& types =
            deviceManager_.getAvailableDeviceTypes();
        std::string typeList = "device types:";
        for (int i = 0; i < types.size(); ++i) {
            typeList += " [" + types[i]->getTypeName().toStdString() + "]";
        }
        crashLog(typeList.c_str());
    }
    if (err.isNotEmpty()) {
        statusLabel_->setText("audio init: " + err, juce::dontSendNotification);
    }
    startTimer(kTimerMs);
    rebuildPresetList();
    pendingUnmute_ = juce::Time::getMillisecondCounter() + 1500;
    crashLog("lifecycle: initialise done");
}

void NAMEditorApplication::shutdown()
{
    crashLog("lifecycle: shutdown");
    deviceManager_.removeAudioCallback(&player_);
    player_.setSource(nullptr);
    deviceManager_.closeAudioDevice();
    mainWindow_ = nullptr;
}

void NAMEditorApplication::systemRequestedQuit()
{
    quit();
}

void NAMEditorApplication::timerCallback()
{
    if (++aliveTicks_ % 50 == 0) {
        crashLog("alive tick"); // 5 s heartbeat for crash diagnostics
        // under/overrun counter: growing xruns mean the buffer size is too
        // small for the device (the classic cause of "explosive" audio)
        const int xr = deviceManager_.getXRunCount();
        xrunLabel_->setText(juce::String("xrun: ")
                                + (xr < 0 ? juce::String("n/a") : juce::String(xr)),
                            juce::dontSendNotification);
    }
    // after a device switch the output stays muted until the new device has
    // warmed up (filter states settle), then fades back in
    if (pendingUnmute_ != 0 && juce::Time::getMillisecondCounter() >= pendingUnmute_) {
        pendingUnmute_ = 0;
        host_.output().setMute(false);
    }
    if (tunerOn_) {
        updateTunerReadout();
    }
    if (host_.sceneCount() > 0) {
        sceneLabel_->setText("Scene: " + juce::String(host_.activeScene() + 1) + "/"
                                 + juce::String(host_.sceneCount()),
                             juce::dontSendNotification);
    }
}

void NAMEditorApplication::updateTunerReadout()
{
    const dsp::Tuner& tuner = host_.tuner();
    // English readouts: JUCE's bundled typefaces have no CJK glyphs and the
    // system-font fallback is unreliable, so Chinese text would garble
    if (!tuner.noteDetected()) {
        tunerMeter_->setDeviation(0.0f, false, false);
        tunerLabel_->setText("no signal (A4=440Hz)", juce::dontSendNotification);
        return;
    }
    // auto-match the string nearest the detected pitch: the player plucks a
    // string and the readout names it plus the deviation in cents
    const int detected = tuner.midiNote();
    int best = 0;
    int bestDist = 99;
    for (int i = 0; i < kStringCount; ++i) {
        const int d = std::abs(detected - kTargetNotes[tuning_][i]);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    static const char* kStringNames[6] = {"1stE4", "2ndB3", "3rdG3", "4thD3", "5thA2", "6thE2"};
    if (bestDist > 2) {
        tunerMeter_->setDeviation(0.0f, false, false);
        tunerLabel_->setText("detected " + noteName(detected) + " ("
                                 + juce::String(tuner.frequency(), 1) + " Hz), not near any "
                                 + juce::String(kTuningNames[tuning_]) + " string",
                             juce::dontSendNotification);
        return;
    }
    const int target = kTargetNotes[tuning_][best];
    const float dev = static_cast<float>(detected - target) * 100.0f + tuner.cents();
    const bool inTune = std::fabs(dev) <= 5.0f;
    const juce::String status = (dev > 5.0f) ? "sharp"
                                : ((dev < -5.0f) ? "flat" : "in tune");
    tunerMeter_->setDeviation(dev, true, inTune);
    tunerLabel_->setText(juce::String(kStringNames[best]) + " target " + noteName(target)
                             + " | " + juce::String(tuner.frequency(), 1) + " Hz ("
                             + noteName(detected) + ") | " + juce::String(dev, 0) + "ct "
                             + status,
                         juce::dontSendNotification);
}

void NAMEditorApplication::rebuildPresetList()
{
    presetBox_->clear(juce::dontSendNotification);
    if (!demoDir_.isDirectory()) {
        statusLabel_->setText("demo preset dir not found: " + demoDir_.getFullPathName(),
                              juce::dontSendNotification);
        return;
    }
    juce::Array<juce::File> presets;
    demoDir_.findChildFiles(presets, juce::File::findFiles, false, "*.json");
    for (const juce::File& f : presets) {
        presetBox_->addItem(f.getFileNameWithoutExtension(), presetBox_->getNumItems() + 1);
    }
    statusLabel_->setText(juce::String(presets.size()) + " demo presets",
                          juce::dontSendNotification);
}

void NAMEditorApplication::loadSelectedPreset()
{
    const int idx = presetBox_->getSelectedId();
    if (idx <= 0) {
        return;
    }
    juce::Array<juce::File> presets;
    demoDir_.findChildFiles(presets, juce::File::findFiles, false, "*.json");
    if (idx > presets.size()) {
        return;
    }
    const juce::File file = presets[static_cast<int>(idx - 1)];
    std::string error;
    const bool ok = host_.loadPreset(file.getFullPathName().toStdString(),
                                     demoDir_.getFullPathName().toStdString(), error);
    if (ok) {
        statusLabel_->setText("loaded " + file.getFileName(), juce::dontSendNotification);
        chainLabel_->setText(juce::String(host_.chainSummary()), juce::dontSendNotification);
    } else {
        statusLabel_->setText("load failed: " + juce::String(error), juce::dontSendNotification);
    }
}

void NAMEditorApplication::refreshAudioDeviceControls()
{
    const juce::OwnedArray<juce::AudioIODeviceType>& types = deviceManager_.getAvailableDeviceTypes();
    deviceTypeBox_->clear(juce::dontSendNotification);
    const juce::String currentType = deviceManager_.getCurrentAudioDeviceType();
    int currentTypeId = 1;
    for (int i = 0; i < types.size(); ++i) {
        deviceTypeBox_->addItem(types[i]->getTypeName(), i + 1);
        if (types[i]->getTypeName() == currentType) {
            currentTypeId = i + 1;
        }
    }
    deviceTypeBox_->setSelectedId(currentTypeId, juce::dontSendNotification);

    deviceBox_->clear(juce::dontSendNotification);
    auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (type != nullptr) {
        const juce::StringArray names = type->getDeviceNames(true);
        for (int i = 0; i < names.size(); ++i) {
            deviceBox_->addItem(names[i], i + 1);
        }
        auto* dev = deviceManager_.getCurrentAudioDevice();
        if (dev != nullptr) {
            deviceBox_->setText(dev->getName(), juce::dontSendNotification);
        }
    }

    sampleRateBox_->clear(juce::dontSendNotification);
    const int rates[] = {44100, 48000, 88200, 96000};
    for (int r : rates) {
        sampleRateBox_->addItem(juce::String(r), r);
    }
    bufferSizeBox_->clear(juce::dontSendNotification);
    const int bufs[] = {64, 128, 256, 512, 1024};
    for (int b : bufs) {
        bufferSizeBox_->addItem(juce::String(b), b);
    }
    auto* dev = deviceManager_.getCurrentAudioDevice();
    if (dev != nullptr) {
        sampleRateBox_->setSelectedId(static_cast<int>(dev->getCurrentSampleRate()),
                                      juce::dontSendNotification);
        bufferSizeBox_->setSelectedId(dev->getCurrentBufferSizeSamples(),
                                      juce::dontSendNotification);
    }
}

void NAMEditorApplication::applyAudioSetup()
{
    const juce::OwnedArray<juce::AudioIODeviceType>& types = deviceManager_.getAvailableDeviceTypes();
    const int typeIdx = deviceTypeBox_->getSelectedId();
    const int devIdx = deviceBox_->getSelectedId();
    if (typeIdx <= 0 || typeIdx > types.size() || devIdx <= 0) {
        return;
    }
    const juce::String typeName = types[typeIdx - 1]->getTypeName();
    const juce::String deviceName = deviceBox_->getItemText(devIdx - 1);
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.outputDeviceName = deviceName;
    setup.inputDeviceName = deviceName;
    setup.sampleRate = static_cast<double>(sampleRateBox_->getSelectedId());
    setup.bufferSize = bufferSizeBox_->getSelectedId();
    // mute BEFORE any device restart (both the type switch and the setup
    // change stop the callback) and hold it until the new device warms up
    host_.output().setMute(true);
    deviceManager_.setCurrentAudioDeviceType(typeName, false);
    const juce::String err = deviceManager_.setAudioDeviceSetup(setup, true);
    host_.output().reset(); // clean filter states on the new device rate
    pendingUnmute_ = juce::Time::getMillisecondCounter() + 2000;
    if (err.isNotEmpty()) {
        statusLabel_->setText("audio setup error: " + err, juce::dontSendNotification);
        return;
    }
    auto* dev = deviceManager_.getCurrentAudioDevice();
    statusLabel_->setText(
        juce::String("audio: ") + typeName + " / " + deviceName + " @ "
            + (dev != nullptr ? juce::String(static_cast<int>(dev->getCurrentSampleRate()))
                              : juce::String(static_cast<int>(setup.sampleRate)))
            + " Hz, "
            + (dev != nullptr ? juce::String(dev->getCurrentBufferSizeSamples())
                              : juce::String(setup.bufferSize))
            + " samples",
        juce::dontSendNotification);
}

void NAMEditorApplication::buildUi()
{
    setSize(1280, 720);
    presetBox_ = std::make_unique<juce::ComboBox>();
    presetBox_->setBounds(16, 16, 360, 28);
    addAndMakeVisible(*presetBox_);
    presetBox_->onChange = [this] { loadSelectedPreset(); };

    loadButton_ = std::make_unique<juce::TextButton>("Load");
    loadButton_->setBounds(384, 16, 72, 28);
    addAndMakeVisible(*loadButton_);
    loadButton_->onClick = [this] { loadSelectedPreset(); };

    statusLabel_ = std::make_unique<juce::Label>();
    statusLabel_->setBounds(16, 52, 560, 20);
    addAndMakeVisible(*statusLabel_);

    xrunLabel_ = std::make_unique<juce::Label>();
    xrunLabel_->setBounds(580, 52, 200, 20);
    addAndMakeVisible(*xrunLabel_);

    // audio device panel: type / device / sample rate / buffer size
    deviceTypeBox_ = std::make_unique<juce::ComboBox>();
    deviceTypeBox_->setBounds(16, 80, 240, 26);
    addAndMakeVisible(*deviceTypeBox_);
    deviceTypeBox_->onChange = [this] {
        const int typeIdx = deviceTypeBox_->getSelectedId();
        const juce::OwnedArray<juce::AudioIODeviceType>& types =
            deviceManager_.getAvailableDeviceTypes();
        if (typeIdx > 0 && typeIdx <= types.size()) {
            // mute BEFORE the device restarts (the type switch itself stops
            // the callback) and hold it until the new device warms up
            host_.output().setMute(true);
            deviceManager_.setCurrentAudioDeviceType(types[typeIdx - 1]->getTypeName(), false);
            refreshAudioDeviceControls();
            pendingUnmute_ = juce::Time::getMillisecondCounter() + 2000;
        }
    };

    deviceBox_ = std::make_unique<juce::ComboBox>();
    deviceBox_->setBounds(264, 80, 300, 26);
    addAndMakeVisible(*deviceBox_);

    sampleRateBox_ = std::make_unique<juce::ComboBox>();
    sampleRateBox_->setBounds(572, 80, 100, 26);
    addAndMakeVisible(*sampleRateBox_);

    bufferSizeBox_ = std::make_unique<juce::ComboBox>();
    bufferSizeBox_->setBounds(680, 80, 100, 26);
    addAndMakeVisible(*bufferSizeBox_);

    applyAudioButton_ = std::make_unique<juce::TextButton>("Apply");
    applyAudioButton_->setBounds(788, 80, 60, 26);
    addAndMakeVisible(*applyAudioButton_);
    applyAudioButton_->onClick = [this] { applyAudioSetup(); };

    // tuner panel: tuning selection + graphical deviation meter
    tuningBox_ = std::make_unique<juce::ComboBox>();
    tuningBox_->setBounds(16, 116, 120, 26);
    addAndMakeVisible(*tuningBox_);
    tuningBox_->addItem("EADGBE", 1);
    tuningBox_->addItem("Drop D", 2);
    tuningBox_->setSelectedId(1, juce::dontSendNotification);
    tuningBox_->onChange = [this] {
        tuning_ = tuningBox_->getSelectedId() - 1;
        if (tuning_ < 0 || tuning_ >= kTuningCount) {
            tuning_ = 0;
        }
    };

    tunerMeter_ = std::make_unique<TunerMeter>();
    tunerMeter_->setBounds(144, 112, 480, 34);
    addAndMakeVisible(*tunerMeter_);

    // CJK-capable font: JUCE's default typeface has no Chinese glyphs
    const juce::Font cjk = juce::Font(juce::FontOptions().withName("Microsoft YaHei UI")
                                          .withHeight(13.0f));
    tunerLabel_ = std::make_unique<juce::Label>();
    tunerLabel_->setBounds(636, 116, 620, 24);
    tunerLabel_->setFont(cjk);
    addAndMakeVisible(*tunerLabel_);

    tunerToggle_ = std::make_unique<juce::ToggleButton>("Tuner");
    tunerToggle_->setBounds(16, 150, 88, 24);
    tunerToggle_->setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(*tunerToggle_);
    tunerToggle_->onClick = [this] {
        tunerOn_ = tunerToggle_->getToggleState();
        if (!tunerOn_) {
            tunerMeter_->setDeviation(0.0f, false, false);
            tunerLabel_->setText("tuner off", juce::dontSendNotification);
        }
    };

    sceneLabel_ = std::make_unique<juce::Label>();
    sceneLabel_->setBounds(120, 150, 400, 20);
    addAndMakeVisible(*sceneLabel_);

    // chain view: what is actually loaded (module per slot + parameter
    // values), read from the engine after a load
    chainLabel_ = std::make_unique<juce::TextEditor>();
    chainLabel_->setMultiLine(true);
    chainLabel_->setReadOnly(true);
    chainLabel_->setScrollbarsShown(true);
    chainLabel_->setBounds(16, 176, 1240, 520);
    addAndMakeVisible(*chainLabel_);
    chainLabel_->setText("(no preset loaded)", juce::dontSendNotification);
}

void NAMEditorApplication::paint(juce::Graphics&)
{
}

} // namespace desktop
} // namespace namfx

// JUCE's application macro needs the class name at global scope; the crash
// handlers are installed in initialise() (process-wide)
START_JUCE_APPLICATION(namfx::desktop::NAMEditorApplication)
