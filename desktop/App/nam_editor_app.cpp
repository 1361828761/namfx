#include "desktop/App/nam_editor_app.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
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

// --- NAM model classification -------------------------------------------
// NAM files carry a JSON header with gear metadata ("gear_type" / "gear_make"
// / "name"); read the first 64 KB and classify into type (Amp/Cab/Pedal) +
// brand (Vox/Marshall/...) so the library can be grouped.

juce::String extractJsonString(const juce::String& head, const juce::String& key)
{
    const int p = head.indexOf("\"" + key + "\"");
    if (p < 0) {
        return {};
    }
    const int c = head.indexOf(p + key.length() + 2, ":");
    if (c < 0) {
        return {};
    }
    const int q1 = head.indexOf(c + 1, "\"");
    if (q1 < 0) {
        return {};
    }
    const int q2 = head.indexOf(q1 + 1, "\"");
    if (q2 < 0) {
        return {};
    }
    return head.substring(q1 + 1, q2);
}

juce::String modelTypeFor(const juce::String& gearType, const juce::String& name)
{
    const juce::String t = gearType.toLowerCase();
    // combined amp+cab captures (ToneHunt gear_type "amp_cab")
    if (t.contains("amp_cab") || t == "ampcab"
        || (t.contains("amp") && t.contains("cab"))) {
        return "Amp+Cab";
    }
    if (t.contains("amp") || t.contains("preamp")) {
        return "Amp";
    }
    if (t.contains("cab")) {
        return "Cab";
    }
    const juce::String n = name.toLowerCase();
    if (n.contains("cab") || n.contains("ir") || n.contains("412") || n.contains("212")) {
        return "Cab";
    }
    if (n.contains("amp") || n.contains("head")) {
        return "Amp";
    }
    return "Pedal";
}

juce::String modelBrandFor(const juce::String& make, const juce::String& name)
{
    const juce::String all = (make + " " + name).toLowerCase();
    if (all.contains("ac30") || all.contains("vox")) {
        return "Vox";
    }
    if (all.contains("marshall") || all.contains("mars") || all.contains("plexi")
        || all.contains("jcm") || all.contains("1959")) {
        return "Marshall";
    }
    if (all.contains("fender") || all.contains("deluxe") || all.contains("twin")
        || all.contains("princeton") || all.contains("reverb")) {
        return "Fender";
    }
    if (all.contains("ocd")) {
        return "Fulltone";
    }
    if (all.contains("centaur") || all.contains("klon")) {
        return "Klon";
    }
    if (all.contains("rat")) {
        return "ProCo";
    }
    if (all.contains("od3") || all.contains("boss")) {
        return "Boss";
    }
    if (all.contains("bno") || all.contains("jt45") || all.contains("jtm")) {
        return "BNO";
    }
    if (all.contains("5150")) {
        return "EVH";
    }
    const juce::String first = make.upToFirstOccurrenceOf(" ", false, false);
    return first.isNotEmpty() ? first : "Other";
}

// (type, brand, display name) of a model file; reads the JSON header
struct ModelClass {
    juce::String type;
    juce::String brand;
    juce::String name;
};

ModelClass classifyModel(const juce::File& f)
{
    ModelClass c;
    c.name = f.getFileNameWithoutExtension();
    juce::FileInputStream in(f);
    if (in.openedOk()) {
        const auto len = std::min<juce::int64>(65536, in.getTotalLength());
        juce::MemoryBlock mb;
        in.readIntoMemoryBlock(mb, static_cast<juce::ssize_t>(len));
        const juce::String head(mb.toString().substring(0, static_cast<int>(len)));
        const juce::String gearType = extractJsonString(head, "gear_type");
        const juce::String gearMake = extractJsonString(head, "gear_make");
        const juce::String jsonName = extractJsonString(head, "name");
        if (gearMake.isNotEmpty()) {
            c.name = jsonName.isNotEmpty() ? jsonName : c.name;
            c.type = modelTypeFor(gearType, gearMake);
            c.brand = modelBrandFor(gearMake, jsonName);
            return c;
        }
    }
    // no metadata: fall back to the filename
    c.type = modelTypeFor("", c.name);
    c.brand = modelBrandFor("", c.name);
    return c;
}

} // namespace

const char* NAMEditorApplication::kTuningNames[kTuningCount] = {"EADGBE", "Drop D"};

MainWindow::MainWindow(juce::Component& content)
    : juce::DocumentWindow("NAM Editor", juce::Colours::darkgrey,
                           juce::DocumentWindow::allButtons)
{
    setContentNonOwned(&content, false);
    setResizable(true, false);
    setResizeLimits(1100, 680, 3200, 2200);
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
    juce::LookAndFeel::setDefaultLookAndFeel(&theme_);
    // demo presets: exe-adjacent "presets" folder first (packaged build),
    // then the compile-time source path, then the working directory
    demoDir_ = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                   .getParentDirectory()
                   .getChildFile("presets");
    if (!demoDir_.isDirectory()) {
        demoDir_ = juce::File(NAMFX_DEMO_DIR);
    }
    if (!demoDir_.isDirectory()) {
        demoDir_ = juce::File::getCurrentWorkingDirectory().getChildFile("core/preset/demo");
    }
    userPresetDir_ = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile("namfx")
                         .getChildFile("presets");
    midiBindFile_ = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("namfx")
                        .getChildFile("midi_binds.txt");
    settingsFile_ = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("namfx")
                        .getChildFile("settings.txt");
    // rotate an oversized crash log
    juce::File crashLogFile(juce::File::getCurrentWorkingDirectory().getChildFile("namfx_crash.log"));
    if (crashLogFile.getSize() > 1024 * 1024) {
        crashLogFile.deleteFile();
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
    // MIDI input: enable every available input device (pedal / interface)
    midiCallback_ = std::make_unique<MidiInputCallback>(*this);
    for (const juce::MidiDeviceInfo& dev : juce::MidiInput::getAvailableDevices()) {
        deviceManager_.setMidiInputDeviceEnabled(dev.identifier, true);
        deviceManager_.addMidiInputDeviceCallback(dev.identifier, midiCallback_.get());
    }
    buildUi();
    loadSettings(); // restore output panel values (sliders now exist)
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
    // load a default preset so the chain is never empty at startup
    juce::File defaultPreset = demoDir_.getChildFile("clean.json");
    if (!defaultPreset.exists()) {
        defaultPreset = demoDir_.getChildFile("boost.json");
    }
    if (defaultPreset.exists()) {
        loadPresetFile(defaultPreset);
    }
    loadMidiBinds(); // restore persisted CC bindings (needs a chain)
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
    // warmed up (filter states settle), then fades back in; a manual Mute
    // keeps the output muted
    if (pendingUnmute_ != 0 && juce::Time::getMillisecondCounter() >= pendingUnmute_) {
        pendingUnmute_ = 0;
        if (muteToggle_ == nullptr || !muteToggle_->getToggleState()) {
            host_.output().setMute(false);
        }
    }
    if (tunerOn_) {
        updateTunerReadout();
    }
    if (host_.sceneCount() > 0) {
        sceneLabel_->setText("Scene: " + juce::String(host_.activeScene() + 1) + "/"
                                 + juce::String(host_.sceneCount()),
                             juce::dontSendNotification);
    }
    // level readouts (10 Hz)
    if (aliveTicks_ % 10 == 0) {
        auto dbText = [](float level) {
            if (level < 1e-5f) {
                return juce::String("-inf dB");
            }
            return juce::String(20.0 * std::log10(level), 1) + " dB";
        };
        levelInLabel_->setText("in " + dbText(host_.inputLevel()), juce::dontSendNotification);
        levelOutLabel_->setText("out " + dbText(host_.outputLevel()),
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
    presetFiles_.clear();
    if (!demoDir_.isDirectory()) {
        statusLabel_->setText("demo preset dir not found: " + demoDir_.getFullPathName(),
                              juce::dontSendNotification);
        return;
    }
    juce::Array<juce::File> presets;
    demoDir_.findChildFiles(presets, juce::File::findFiles, false, "*.json");
    presets.sort();
    presetBox_->addSectionHeading("Demo");
    for (const juce::File& f : presets) {
        presetFiles_.push_back(f);
        presetBox_->addItem(f.getFileNameWithoutExtension(),
                            static_cast<int>(presetFiles_.size()));
    }
    // user presets (Documents/namfx/presets)
    userPresetDir_.createDirectory();
    juce::Array<juce::File> mine;
    userPresetDir_.findChildFiles(mine, juce::File::findFiles, false, "*.json");
    mine.sort();
    if (!mine.isEmpty()) {
        presetBox_->addSectionHeading("Mine");
        for (const juce::File& f : mine) {
            presetFiles_.push_back(f);
            presetBox_->addItem(f.getFileNameWithoutExtension(),
                                static_cast<int>(presetFiles_.size()));
        }
    }
    if (!presetFiles_.empty()) {
        presetBox_->setSelectedId(1, juce::dontSendNotification);
    }
    statusLabel_->setText(juce::String(presetFiles_.size()) + " presets",
                          juce::dontSendNotification);
    rebuildPresetSidebar();
}

void NAMEditorApplication::loadPresetFile(const juce::File& file)
{
    std::string error;
    const bool ok = host_.loadPreset(file.getFullPathName().toStdString(),
                                     demoDir_.getFullPathName().toStdString(), error);
    if (ok) {
        statusLabel_->setText("loaded " + file.getFileName(), juce::dontSendNotification);
        refreshChainViews();
        rebuildSceneBar();
        selectedScene_ = -1;
    } else {
        statusLabel_->setText("load failed: " + juce::String(error), juce::dontSendNotification);
    }
}

void NAMEditorApplication::loadSelectedPreset()
{
    const int idx = presetBox_->getSelectedId();
    if (idx <= 0 || idx > static_cast<int>(presetFiles_.size())) {
        return;
    }
    loadPresetFile(presetFiles_[static_cast<std::size_t>(idx - 1)]);
}

void NAMEditorApplication::refreshModelLibrary()
{
    modelFiles_.clear();
    irFiles_.clear();
    std::vector<juce::File> dirs;
    dirs.push_back(juce::File(NAMFX_NAM_DIR));
    dirs.push_back(demoDir_.getChildFile("models"));
    dirs.push_back(juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                       .getParentDirectory()
                       .getChildFile("models"));
    for (const juce::File& d : dirs) {
        if (!d.isDirectory()) {
            continue;
        }
        juce::Array<juce::File> found;
        d.findChildFiles(found, juce::File::findFiles, false, "*.nam");
        for (const juce::File& f : found) {
            modelFiles_.push_back(f);
        }
    }
    // IR library: demo IRs + user IR folder + exe-adjacent folder
    std::vector<juce::File> irDirs;
    irDirs.push_back(demoDir_.getChildFile("irs"));
    irDirs.push_back(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile("namfx")
                         .getChildFile("irs"));
    irDirs.push_back(juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                         .getParentDirectory()
                         .getChildFile("irs"));
    for (const juce::File& d : irDirs) {
        if (!d.isDirectory()) {
            continue;
        }
        juce::Array<juce::File> found;
        d.findChildFiles(found, juce::File::findFiles, false, "*.wav");
        for (const juce::File& f : found) {
            irFiles_.push_back(f);
        }
    }
    std::sort(irFiles_.begin(), irFiles_.end(),
              [](const juce::File& a, const juce::File& b) {
                  return a.getFileName().compareNatural(b.getFileName()) < 0;
              });
    // stable, human-friendly order: type -> brand -> name
    std::sort(modelFiles_.begin(), modelFiles_.end(),
              [](const juce::File& a, const juce::File& b) {
                  const ModelClass ca = classifyModel(a);
                  const ModelClass cb = classifyModel(b);
                  if (ca.type != cb.type) {
                      return ca.type < cb.type;
                  }
                  if (ca.brand != cb.brand) {
                      return ca.brand < cb.brand;
                  }
                  return a.getFileName().compareNatural(b.getFileName()) < 0;
              });
}

// module categories with EXPLICIT module ids and an asset-type filter:
// "amp" -> NAM amp/amp+cab models, "pedal" -> NAM pedal models, "ir" -> IR
// files, "" -> no asset. Amp and pedal NAM captures stay apart.
struct NAMGroupDef {
    const char* name;
    const char* assetType;
    std::vector<const char*> ids;
};

static const NAMGroupDef kNAMGroups[] = {
    {"Amp (NAM)", "amp", {"amp.nam"}},
    {"Pedal (NAM)", "pedal", {"amp.nam"}},
    {"Cabinet", "ir", {"cab.ir"}},
    {"Distortion", "", {"od.ts808", "od.transparent", "od.mosfet"}},
    {"Compressor", "", {"comp.ota"}},
    {"Noise Gate", "", {"gate.ns2"}},
    {"Modulation", "", {"mod.chorus", "mod.flanger", "mod.phaser", "mod.wah"}},
    {"Delay", "", {"dly.dm2", "dly.tape"}},
    {"Reverb", "", {"rvb.hall", "rvb.spring"}},
    {"Pitch", "", {"pitch.shift", "pitch.octave"}},
    {"EQ", "", {"eq.ge7", "tone"}},
    {"Gain", "", {"gain"}},
};

void NAMEditorApplication::rebuildAddModuleControls()
{
    addGroupBox_->clear(juce::dontSendNotification);
    for (int g = 0; g < static_cast<int>(std::size(kNAMGroups)); ++g) {
        addGroupBox_->addItem(kNAMGroups[g].name, g + 1);
    }
    if (addGroupBox_->getNumItems() > 0) {
        addGroupBox_->setSelectedId(1, juce::dontSendNotification);
    }
    refreshModuleList();
}

void NAMEditorApplication::refreshModuleList()
{
    const int g = addGroupBox_->getSelectedId() - 1;
    const std::vector<std::string> ids = host_.moduleIds();

    addModuleBox_->clear(juce::dontSendNotification);
    if (g < 0 || g >= static_cast<int>(std::size(kNAMGroups))) {
        return;
    }
    for (const char* id : kNAMGroups[g].ids) {
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
            addModuleBox_->addItem(id, static_cast<int>(addModuleBox_->getNumItems()) + 1);
        }
    }
    if (addModuleBox_->getNumItems() > 0) {
        addModuleBox_->setSelectedId(1, juce::dontSendNotification);
    }
    refreshAssetList();
}

void NAMEditorApplication::refreshAssetList()
{
    const int g = addGroupBox_->getSelectedId() - 1;
    const bool hasAsset =
        g >= 0 && g < static_cast<int>(std::size(kNAMGroups)) && kNAMGroups[g].assetType[0] != '\0';
    addAssetBox_->setVisible(hasAsset);
    addAssetBox_->clear(juce::dontSendNotification);
    if (!hasAsset) {
        return;
    }
    const juce::String assetType = kNAMGroups[g].assetType;
    if (assetType == "ir") {
        // IR library (flat list)
        for (std::size_t i = 0; i < irFiles_.size(); ++i) {
            addAssetBox_->addItem(irFiles_[i].getFileName(), static_cast<int>(i + 1));
        }
        if (addAssetBox_->getNumItems() > 0) {
            addAssetBox_->setSelectedId(1, juce::dontSendNotification);
        }
        return;
    }
    // NAM models filtered by the group's type: "amp" shows Amp/Amp+Cab
    // captures, "pedal" shows stompbox captures
    const bool wantPedal = (assetType == "pedal");
    juce::String currentGroup;
    for (std::size_t i = 0; i < modelFiles_.size(); ++i) {
        const ModelClass c = classifyModel(modelFiles_[i]);
        const bool isAmp = (c.type == "Amp" || c.type == "Amp+Cab");
        if (wantPedal ? (c.type != "Pedal") : !isAmp) {
            continue;
        }
        const juce::String group = c.type + " / " + c.brand;
        if (group != currentGroup) {
            currentGroup = group;
            addAssetBox_->addSectionHeading(group);
        }
        addAssetBox_->addItem(c.name, static_cast<int>(i + 1));
    }
    if (addAssetBox_->getNumItems() > 0) {
        addAssetBox_->setSelectedId(1, juce::dontSendNotification);
    }
}

void NAMEditorApplication::addSectionLabel(const juce::String& text, int x, int y)
{
    auto* l = new juce::Label();
    l->setText(text, juce::dontSendNotification);
    l->setFont(juce::Font(juce::FontOptions().withHeight(11.0f).withStyleFlags(
        juce::Font::bold)));
    l->setColour(juce::Label::textColourId, NAMTheme::textDim());
    l->setBounds(x, y, 200, 14);
    addAndMakeVisible(l);
    sectionLabels_.push_back(l);
}

void NAMEditorApplication::saveUserPreset()
{
    const juce::String name = presetNameBox_->getText().trim();
    if (name.isEmpty()) {
        statusLabel_->setText("enter a preset name first", juce::dontSendNotification);
        return;
    }
    if (name.containsChar('/') || name.containsChar('\\') || name.containsChar('.')) {
        statusLabel_->setText("invalid name (no / \\ .)", juce::dontSendNotification);
        return;
    }
    // user preset cap: 100
    juce::Array<juce::File> mine;
    userPresetDir_.findChildFiles(mine, juce::File::findFiles, false, "*.json");
    if (mine.size() >= 100) {
        statusLabel_->setText("preset limit reached (100)", juce::dontSendNotification);
        return;
    }
    std::string error;
    if (host_.savePreset(name.toStdString(), userPresetDir_.getFullPathName().toStdString(),
                         error)) {
        statusLabel_->setText("saved " + name, juce::dontSendNotification);
        presetNameBox_->clear();
        rebuildPresetList();
        // select the newly saved preset
        for (std::size_t i = 0; i < presetFiles_.size(); ++i) {
            if (presetFiles_[i].getFileNameWithoutExtension() == name) {
                presetBox_->setSelectedId(static_cast<int>(i + 1), juce::dontSendNotification);
                break;
            }
        }
    } else {
        statusLabel_->setText("save failed: " + juce::String(error), juce::dontSendNotification);
    }
}

GripLabel::GripLabel(int slot, NAMEditorApplication& app, ChainPanelContent& content)
    : slot_(slot), app_(app), content_(content)
{
    setText("::", juce::dontSendNotification);
    setColour(juce::Label::textColourId, NAMTheme::textDim());
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void GripLabel::mouseDown(const juce::MouseEvent&)
{
    dragging_ = true;
}

void GripLabel::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_) {
        return;
    }
    // map the mouse into the panel and update the insertion indicator
    const juce::Point<int> p = content_.getLocalPoint(e.eventComponent, e.getPosition());
    content_.setDropIndexAt(p.getX());
}

void GripLabel::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    const int dst = content_.takeDropIndex();
    if (dst >= 0) {
        app_.reorderChainByDrag(slot_, dst);
    }
}

void ChainPanelContent::setDropIndexAt(int x)
{
    int row = 0;
    for (int i = 0; i < static_cast<int>(rowBounds_.size()); ++i) {
        if (x < rowBounds_[static_cast<std::size_t>(i)]) {
            row = i;
            break;
        }
        row = i + 1;
    }
    dropIndex_ = row;
    repaint();
}

int ChainPanelContent::takeDropIndex()
{
    const int d = dropIndex_;
    dropIndex_ = -1;
    repaint();
    return d;
}

void ChainPanelContent::paint(juce::Graphics& g)
{
    g.fillAll(NAMTheme::bg());
    int left = 8;
    for (std::size_t i = 0; i < rowBounds_.size(); ++i) {
        const int right = rowBounds_[i] - 4;
        const auto card = juce::Rectangle<float>(static_cast<float>(left), 8.0f,
                                                 static_cast<float>(std::max(right - left, 36)),
                                                 static_cast<float>(getHeight() - 16));
        if (i > 0) {
            g.setColour(NAMTheme::accentDim());
            g.fillRect(static_cast<float>(left - 8), 40.0f, 8.0f, 2.0f);
        }
        g.setColour(i % 2 == 0 ? NAMTheme::panel() : NAMTheme::panelHi().withAlpha(0.72f));
        g.fillRoundedRectangle(card, 8.0f);
        g.setColour(NAMTheme::panelBorder());
        g.drawRoundedRectangle(card, 8.0f, 1.0f);
        left = rowBounds_[i];
    }
    if (dropIndex_ >= 0 && dropIndex_ <= static_cast<int>(rowBounds_.size())) {
        const int x = dropIndex_ == 0 ? 8 : rowBounds_[static_cast<std::size_t>(dropIndex_ - 1)] - 8;
        g.setColour(NAMTheme::accent());
        g.fillRoundedRectangle(static_cast<float>(x), 6.0f, 3.0f,
                               static_cast<float>(getHeight() - 12), 1.5f);
    }
}

void NAMEditorApplication::reorderChainByDrag(int srcSlot, int dstIndex)
{
    std::string error;
    if (host_.moveModuleTo(srcSlot, dstIndex, error)) {
        refreshChainViews();
    } else {
        statusLabel_->setText("reorder failed: " + juce::String(error),
                              juce::dontSendNotification);
    }
}

void MidiInputCallback::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg)
{
    namfx::midi::Event e;
    e.channel = static_cast<std::uint8_t>(msg.getChannel() - 1);
    if (msg.isNoteOn()) {
        e.type = namfx::midi::Event::Type::NoteOn;
        e.data1 = static_cast<std::uint8_t>(msg.getNoteNumber());
        e.data2 = static_cast<std::uint8_t>(msg.getVelocity());
    } else if (msg.isNoteOff()) {
        e.type = namfx::midi::Event::Type::NoteOff;
        e.data1 = static_cast<std::uint8_t>(msg.getNoteNumber());
    } else if (msg.isController()) {
        e.type = namfx::midi::Event::Type::ControlChange;
        e.data1 = static_cast<std::uint8_t>(msg.getControllerNumber());
        e.data2 = static_cast<std::uint8_t>(msg.getControllerValue());
    } else if (msg.isProgramChange()) {
        e.type = namfx::midi::Event::Type::ProgramChange;
        e.data1 = static_cast<std::uint8_t>(msg.getProgramChangeNumber());
    } else if (msg.isPitchWheel()) {
        e.type = namfx::midi::Event::Type::PitchBend;
        e.data1 = static_cast<std::uint8_t>(msg.getPitchWheelValue() & 0x7F);
        e.data2 = static_cast<std::uint8_t>((msg.getPitchWheelValue() >> 7) & 0x7F);
    } else {
        return;
    }
    // dispatch on the UI thread: the engine API is control-thread
    juce::MessageManager::callAsync([this, e] { app_.onMidiEvent(e); });
}

void NAMEditorApplication::onMidiEvent(const midi::Event& event)
{
    host_.handleMidi(event);
    // learn mode: capture the next CC
    if (event.type != midi::Event::Type::ControlChange) {
        return;
    }
    const int cc = event.data1;
    if (learningParam_ != nullptr) {
        std::string error;
        if (host_.midiLearnParam(cc, learningParam_->moduleId, learningParam_->paramId,
                                 error)) {
            midiBinds_.push_back(
                MidiBind{cc, juce::String(learningParam_->moduleId) + "." + juce::String(
                                                                              learningParam_->paramId)});
            midiBindLabel_->setText(bindingSummary(), juce::dontSendNotification);
            statusLabel_->setText(juce::String("learned: CC ") + juce::String(cc) + " -> "
                                      + learningParam_->moduleId + "."
                                      + learningParam_->paramId,
                                  juce::dontSendNotification);
            saveMidiBinds();
        } else {
            statusLabel_->setText("learn failed: " + juce::String(error),
                                  juce::dontSendNotification);
        }
        learningParam_ = nullptr;
        return;
    }
    if (learningScene_ > 0) {
        std::string error;
        if (host_.midiLearnScene(cc, learningScene_, error)) {
            midiBinds_.push_back(
                MidiBind{cc, juce::String("Scene ") + juce::String(learningScene_)});
            midiBindLabel_->setText(bindingSummary(), juce::dontSendNotification);
            statusLabel_->setText(juce::String("learned: CC ") + juce::String(cc) + " -> Scene "
                                      + juce::String(learningScene_),
                                  juce::dontSendNotification);
            saveMidiBinds();
        } else {
            statusLabel_->setText("learn failed: " + juce::String(error),
                                  juce::dontSendNotification);
        }
        learningScene_ = 0;
    }
}

void NAMEditorApplication::beginLearnParam(const std::string& moduleId,
                                           const std::string& paramId)
{
    learningParam_ = std::make_unique<LearnTarget>(LearnTarget{moduleId, paramId});
    statusLabel_->setText("learning: move a pedal CC for " + juce::String(moduleId) + "."
                              + juce::String(paramId),
                          juce::dontSendNotification);
}

void NAMEditorApplication::saveMidiBinds()
{
    midiBindFile_.getParentDirectory().createDirectory();
    juce::FileOutputStream out(midiBindFile_);
    if (!out.openedOk()) {
        return;
    }
    // authoritative snapshot from the engine
    for (const midi::MidiRouter::BindInfo& b : host_.midiBindings()) {
        if (b.kind == midi::MidiRouter::BindInfo::Kind::Param) {
            out << juce::String("param ") << juce::String(b.cc) << juce::String(" ")
                << juce::String(b.moduleId) << juce::String(".") << juce::String(b.paramId)
                << juce::String("\n");
        } else {
            out << juce::String("scene ") << juce::String(b.cc) << juce::String(" ")
                << juce::String(b.sceneIndex) << juce::String("\n");
        }
    }
    out.flush();
}

void NAMEditorApplication::loadMidiBinds()
{
    if (!midiBindFile_.existsAsFile()) {
        return;
    }
    juce::FileInputStream in(midiBindFile_);
    if (!in.openedOk()) {
        return;
    }
    midiBinds_.clear();
    while (!in.isExhausted()) {
        const juce::String line = in.readNextLine().trim();
        if (line.isEmpty() || line.startsWithChar('#')) {
            continue;
        }
        juce::StringArray parts;
        parts.addTokens(line, true);
        if (parts.size() < 2) {
            continue;
        }
        midi::MidiRouter::BindInfo b;
        b.cc = parts[1].getIntValue();
        if (parts[0] == "param" && parts.size() >= 3) {
            // moduleId may contain dots: split at the LAST dot
            const juce::String target = parts[2];
            const int dot = target.lastIndexOfChar('.');
            if (dot <= 0) {
                continue;
            }
            b.kind = midi::MidiRouter::BindInfo::Kind::Param;
            b.moduleId = target.substring(0, dot).toStdString();
            b.paramId = target.substring(dot + 1).toStdString();
        } else if (parts[0] == "scene" && parts.size() >= 3) {
            b.kind = midi::MidiRouter::BindInfo::Kind::Scene;
            b.sceneIndex = parts[2].getIntValue();
        } else {
            continue;
        }
        std::string error;
        if (host_.midiRestoreBind(b, error)) {
            if (b.kind == midi::MidiRouter::BindInfo::Kind::Param) {
                midiBinds_.push_back(MidiBind{b.cc, juce::String(b.moduleId) + "."
                                                      + juce::String(b.paramId)});
            } else {
                midiBinds_.push_back(
                    MidiBind{b.cc, juce::String("Scene ") + juce::String(b.sceneIndex)});
            }
        }
    }
    midiBindLabel_->setText(bindingSummary(), juce::dontSendNotification);
}

void NAMEditorApplication::loadSettings()
{
    if (!settingsFile_.existsAsFile()) {
        return;
    }
    juce::FileInputStream in(settingsFile_);
    if (!in.openedOk()) {
        return;
    }
    while (!in.isExhausted()) {
        const juce::String line = in.readNextLine().trim();
        if (line.isEmpty() || line.startsWithChar('#')) {
            continue;
        }
        const int eq = line.indexOfChar('=');
        if (eq <= 0) {
            continue;
        }
        const juce::String key = line.substring(0, eq).trim();
        const double val = line.substring(eq + 1).trim().getDoubleValue();
        if (key == "master") { masterSlider_->setValue(val, juce::sendNotification); }
        else if (key == "ingain") { inputGainSlider_->setValue(val, juce::sendNotification); }
        else if (key == "bass") { bassSlider_->setValue(val, juce::sendNotification); }
        else if (key == "mid") { midSlider_->setValue(val, juce::sendNotification); }
        else if (key == "treble") { trebleSlider_->setValue(val, juce::sendNotification); }
        else if (key == "mute") { muteToggle_->setToggleState(val > 0.5, juce::sendNotification); }
    }
}

void NAMEditorApplication::saveSettings()
{
    settingsFile_.getParentDirectory().createDirectory();
    juce::FileOutputStream out(settingsFile_);
    if (!out.openedOk()) {
        return;
    }
    out << juce::String("master=") << juce::String(masterSlider_->getValue(), 2) << juce::String("\n");
    out << juce::String("ingain=") << juce::String(inputGainSlider_->getValue(), 2) << juce::String("\n");
    out << juce::String("bass=") << juce::String(bassSlider_->getValue(), 3) << juce::String("\n");
    out << juce::String("mid=") << juce::String(midSlider_->getValue(), 3) << juce::String("\n");
    out << juce::String("treble=") << juce::String(trebleSlider_->getValue(), 3) << juce::String("\n");
    out << juce::String("mute=") << (muteToggle_->getToggleState() ? juce::String("1") : juce::String("0")) << juce::String("\n");
    out.flush();
}
void NAMEditorApplication::clearMidiBinds()
{
    for (const MidiBind& b : midiBinds_) {
        host_.midiClearBind(b.cc);
    }
    midiBinds_.clear();
    midiBindLabel_->setText("no MIDI binds", juce::dontSendNotification);
    saveMidiBinds();
}

juce::String NAMEditorApplication::bindingSummary() const
{
    if (midiBinds_.empty()) {
        return "no MIDI binds";
    }
    juce::String s;
    for (const MidiBind& b : midiBinds_) {
        s += "CC " + juce::String(b.cc) + "->" + b.target + "  ";
    }
    return s;
}


void PresetListContent::paint(juce::Graphics& g)
{
    g.fillAll(NAMTheme::bg());
    const int rowH = kRowH;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const int y = static_cast<int>(i) * rowH;
        const bool sel = !items_[i].isSection && items_[i].fileIndex == selected_;
        if (items_[i].isSection) {
            g.setColour(NAMTheme::panelHi());
            g.fillRoundedRectangle(4.0f, static_cast<float>(y + 5),
                                   static_cast<float>(getWidth() - 8), 26.0f, 6.0f);
            g.setColour(NAMTheme::textDim());
            g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyleFlags(juce::Font::bold)));
            g.drawText(items_[i].name.toUpperCase(), 14, y + 10, getWidth() - 28, 16, juce::Justification::left);
        } else {
            const auto row = juce::Rectangle<float>(4.0f, static_cast<float>(y + 3),
                                                    static_cast<float>(getWidth() - 8), 30.0f);
            g.setColour(sel ? NAMTheme::accent() : NAMTheme::panel());
            g.fillRoundedRectangle(row, 6.0f);
            g.setColour(sel ? NAMTheme::bg() : NAMTheme::text());
            g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
            g.drawText(items_[i].name, 14, y + 8, getWidth() - 28, 20, juce::Justification::left);
        }
    }
    setSize(getWidth(), static_cast<int>(items_.size()) * rowH);
}

void PresetListContent::mouseDown(const juce::MouseEvent& e)
{
    const int row = hitTestRow(e.getPosition().getY());
    if (row >= 0 && row < static_cast<int>(items_.size()) && !items_[static_cast<std::size_t>(row)].isSection) {
        selected_ = items_[static_cast<std::size_t>(row)].fileIndex;
        repaint();
        if (onSelect) onSelect(selected_);
    }
}

int PresetListContent::hitTestRow(int y) const
{
    return y / kRowH;
}

void NAMEditorApplication::rebuildPresetSidebar()
{
    std::vector<PresetListContent::Item> items;
    bool inDemo = true;
    items.push_back({"Demo", true, -1});
    for (std::size_t i = 0; i < presetFiles_.size(); ++i) {
        if (inDemo && presetFiles_[i].getParentDirectory() != demoDir_) {
            inDemo = false;
            items.push_back({"Mine", true, -1});
        }
        items.push_back({presetFiles_[i].getFileNameWithoutExtension(), false, static_cast<int>(i)});
    }
    presetListContent_->setItems(items);
    const int sel = presetBox_->getSelectedId() - 1;
    presetListContent_->setSelectedIndex(sel);
    presetListContent_->setSize(presetListViewport_->getWidth() - 12, static_cast<int>(items.size()) * PresetListContent::kRowH);
}

void NAMEditorApplication::rebuildSceneBar()
{
    sceneBar_->removeAllChildren();
    const int n = host_.sceneCount();
    const int active = host_.activeScene();
    for (int i = 0; i < n; ++i) {
        juce::String nm = host_.sceneName(i);
        if (nm.isEmpty()) {
            nm = "Scene " + juce::String(i + 1);
        }
        auto* b = new juce::ToggleButton(nm);
        b->setToggleState(i == active, juce::dontSendNotification);
        b->setBounds(i * 120, 2, 114, 22);
        sceneBar_->addAndMakeVisible(b);
        b->onClick = [this, i] {
            selectedScene_ = i;
            host_.recallScene(i);
            rebuildSceneBar(); // highlight the active scene
        };
    }
    sceneBar_->setSize(std::max(n * 120, 120), 26);
}

void NAMEditorApplication::rebuildChainPanel()
{
    chainPanelContent_->removeAllChildren();
    const std::vector<EngineHost::SlotInfo> infos = host_.chainInfo();
    constexpr int cardW = 270;
    constexpr int cardGap = 12;
    constexpr int top = 8;
    int maxParams = 0;
    for (const EngineHost::SlotInfo& info : infos) {
        maxParams = std::max(maxParams, static_cast<int>(info.specs.size()));
    }
    const int contentH = std::max(300, 106 + maxParams * 24 + 22);
    std::vector<int> bounds;
    for (std::size_t cardIndex = 0; cardIndex < infos.size(); ++cardIndex) {
        const EngineHost::SlotInfo& info = infos[cardIndex];
        const int x = top + static_cast<int>(cardIndex) * (cardW + cardGap);
        auto* grip = new GripLabel(info.slot, *this, *chainPanelContent_);
        grip->setBounds(x + 8, 14, 18, 18);
        chainPanelContent_->addAndMakeVisible(grip);

        auto* name = new juce::Label();
        juce::String nameText = juce::String(info.slot + 1).paddedLeft('0', 2) + "  "
                                + juce::String(info.moduleId);
        name->setText(nameText, juce::dontSendNotification);
        name->setFont(juce::Font(juce::FontOptions().withHeight(13.0f).withStyleFlags(juce::Font::bold)));
        name->setColour(juce::Label::textColourId, NAMTheme::textBright());
        name->setBounds(x + 32, 10, cardW - 42, 24);
        chainPanelContent_->addAndMakeVisible(name);

        auto* asset = new juce::Label();
        asset->setText(info.assetName.empty() ? "Built-in DSP" : juce::String(info.assetName),
                       juce::dontSendNotification);
        asset->setColour(juce::Label::textColourId, NAMTheme::textDim());
        asset->setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        asset->setBounds(x + 14, 34, cardW - 28, 18);
        chainPanelContent_->addAndMakeVisible(asset);

        auto* bp = new juce::ToggleButton("Byp");
        bp->setToggleState(info.bypass, juce::dontSendNotification);
        bp->setBounds(x + 14, 58, 76, 24);
        chainPanelContent_->addAndMakeVisible(bp);
        bp->onClick = [this, slot = info.slot, bp] {
            host_.uiSetBypass(slot, bp->getToggleState());
        };

        auto* mixSl = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        mixSl->setRange(0.0, 1.0, 0.01);
        mixSl->setValue(info.mix, juce::dontSendNotification);
        mixSl->setDoubleClickReturnValue(true, 1.0);
        mixSl->setTooltip("Mix");
        mixSl->setBounds(x + 132, 62, 54, 18);
        chainPanelContent_->addAndMakeVisible(mixSl);
        mixSl->onValueChange = [this, mixSl, slot = info.slot] {
            host_.uiSetMix(slot, static_cast<float>(mixSl->getValue()));
        };

        auto* up = new juce::TextButton("^");
        up->setBounds(x + 190, 58, 22, 24);
        chainPanelContent_->addAndMakeVisible(up);
        up->onClick = [this, slot = info.slot] {
            std::string error;
            host_.moveModule(slot, -1, error);
            refreshChainViews();
        };

        auto* dn = new juce::TextButton("v");
        dn->setBounds(x + 216, 58, 22, 24);
        chainPanelContent_->addAndMakeVisible(dn);
        dn->onClick = [this, slot = info.slot] {
            std::string error;
            host_.moveModule(slot, 1, error);
            refreshChainViews();
        };

        auto* del = new juce::TextButton("Del");
        del->setBounds(x + 242, 58, 24, 24);
        chainPanelContent_->addAndMakeVisible(del);
        del->onClick = [this, slot = info.slot] {
            std::string error;
            host_.removeModuleFromChain(slot, error);
            refreshChainViews();
        };

        int py = 94;
        for (std::size_t p = 0; p < info.specs.size(); ++p) {
            auto* pl = new juce::Label();
            pl->setText(info.specs[p].displayName.empty() ? info.specs[p].id : info.specs[p].displayName,
                        juce::dontSendNotification);
            pl->setColour(juce::Label::textColourId, NAMTheme::text());
            pl->setBounds(x + 14, py, 70, 18);
            chainPanelContent_->addAndMakeVisible(pl);

            auto* sl = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
            sl->setRange(info.specs[p].min, info.specs[p].max, 0.001);
            sl->setValue(info.values[p], juce::dontSendNotification);
            sl->setBounds(x + 88, py + 2, 100, 18);
            chainPanelContent_->addAndMakeVisible(sl);

            auto* vl = new juce::Label();
            vl->setText(juce::String(info.values[p], 2), juce::dontSendNotification);
            vl->setColour(juce::Label::textColourId, NAMTheme::textDim());
            vl->setBounds(x + 192, py, 42, 18);
            chainPanelContent_->addAndMakeVisible(vl);

            auto* lrn = new juce::TextButton("L");
            lrn->setTooltip("MIDI learn");
            lrn->setBounds(x + 238, py - 1, 28, 20);
            chainPanelContent_->addAndMakeVisible(lrn);
            const std::string lrnModuleId = info.moduleId;
            const std::string lrnParamId = info.specs[p].id;
            lrn->onClick = [this, lrnModuleId, lrnParamId] {
                beginLearnParam(lrnModuleId, lrnParamId);
            };

            const int slot = info.slot;
            const std::string paramId = info.specs[p].id;
            sl->setDoubleClickReturnValue(true, info.specs[p].defaultValue);
            sl->onValueChange = [this, sl, vl, slot, paramId] {
                host_.uiSetParam(slot, paramId, static_cast<float>(sl->getValue()));
                vl->setText(juce::String(sl->getValue(), 2), juce::dontSendNotification);
            };
            py += 24;
        }
        bounds.push_back(x + cardW + cardGap);
    }
    const int contentWidth = std::max(chainPanelViewport_->getWidth() - 12, 720);
    const int chainWidth = std::max(contentWidth, top + static_cast<int>(infos.size()) * (cardW + cardGap));
    chainPanelContent_->setSize(chainWidth, contentH);
    chainPanelContent_->setRowBounds(bounds);
    chainPanelViewport_->setViewedComponent(chainPanelContent_.get(), false);
}

void NAMEditorApplication::refreshChainViews()
{
    rebuildChainPanel();
    chainLabel_->setText(juce::String(host_.chainSummary()), juce::dontSendNotification);
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
        const int sr = static_cast<int>(dev->getCurrentSampleRate());
        if (sr > 0) {
            sampleRateBox_->setSelectedId(sr, juce::dontSendNotification);
        }
        const int bs = dev->getCurrentBufferSizeSamples();
        if (bs > 0) {
            bufferSizeBox_->setSelectedId(bs, juce::dontSendNotification);
        }
    }
    // sensible defaults when the device reports nothing (e.g. ASIO before
    // it has been opened): 44.1 kHz / 128 samples
    if (sampleRateBox_->getSelectedId() <= 0) {
        sampleRateBox_->setSelectedId(44100, juce::dontSendNotification);
    }
    if (bufferSizeBox_->getSelectedId() <= 0) {
        bufferSizeBox_->setSelectedId(128, juce::dontSendNotification);
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
    const int selRate = sampleRateBox_->getSelectedId();
    setup.sampleRate = selRate > 0 ? static_cast<double>(selRate) : 44100.0;
    const int selBuf = bufferSizeBox_->getSelectedId();
    setup.bufferSize = selBuf > 0 ? selBuf : 128;
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
    // show the actual values the device ended up with
    refreshAudioDeviceControls();
}

void NAMEditorApplication::buildUi()
{
    setSize(1280, 780);

    const int leftW = 220;
    const int mainX = leftW + 12;
    const int mainW = getWidth() - mainX - 12;

    // --- Left sidebar: preset list ---
    addSectionLabel("PRESETS", 12, 12);
    presetListViewport_ = std::make_unique<juce::Viewport>();
    presetListViewport_->setBounds(12, 32, leftW - 20, getHeight() - 180);
    presetListViewport_->setScrollBarsShown(true, false);
    addAndMakeVisible(*presetListViewport_);
    presetListContent_ = std::make_unique<PresetListContent>();
    presetListContent_->onSelect = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(presetFiles_.size())) {
            presetBox_->setSelectedId(idx + 1, juce::dontSendNotification);
            loadSelectedPreset();
        }
    };
    presetListViewport_->setViewedComponent(presetListContent_.get(), false);

    statusLabel_ = std::make_unique<juce::Label>();
    statusLabel_->setBounds(12, getHeight() - 164, leftW - 20, 18);
    addAndMakeVisible(*statusLabel_);

    xrunLabel_ = std::make_unique<juce::Label>();
    xrunLabel_->setBounds(12, getHeight() - 144, leftW - 20, 16);
    addAndMakeVisible(*xrunLabel_);

    // --- Main area: top control bar ---
    presetBox_ = std::make_unique<juce::ComboBox>();
    presetBox_->setBounds(mainX, 10, 280, 26);
    addAndMakeVisible(*presetBox_);
    presetBox_->onChange = [this] { loadSelectedPreset(); };

    loadButton_ = std::make_unique<juce::TextButton>("Load");
    loadButton_->setBounds(mainX + 288, 10, 56, 26);
    addAndMakeVisible(*loadButton_);
    loadButton_->onClick = [this] { loadSelectedPreset(); };

    delPresetButton_ = std::make_unique<juce::TextButton>("Del");
    delPresetButton_->setBounds(mainX + 350, 10, 40, 26);
    addAndMakeVisible(*delPresetButton_);
    delPresetButton_->onClick = [this] {
        const int idx = presetBox_->getSelectedId();
        if (idx <= 0 || idx > static_cast<int>(presetFiles_.size())) return;
        const juce::File f = presetFiles_[static_cast<std::size_t>(idx - 1)];
        if (f.getParentDirectory() != userPresetDir_) {
            statusLabel_->setText("demo presets cannot be deleted", juce::dontSendNotification);
            return;
        }
        f.deleteFile();
        rebuildPresetList();
        statusLabel_->setText("deleted " + f.getFileName(), juce::dontSendNotification);
    };

    presetNameBox_ = std::make_unique<juce::TextEditor>();
    presetNameBox_->setText("my_tone", false);
    presetNameBox_->setBounds(mainX + 400, 10, 140, 26);
    addAndMakeVisible(*presetNameBox_);

    savePresetButton_ = std::make_unique<juce::TextButton>("Save");
    savePresetButton_->setBounds(mainX + 546, 10, 52, 26);
    addAndMakeVisible(*savePresetButton_);
    savePresetButton_->onClick = [this] { saveUserPreset(); };

    // --- Audio device row ---
    addSectionLabel("AUDIO", mainX, 46);
    deviceTypeBox_ = std::make_unique<juce::ComboBox>();
    deviceTypeBox_->setBounds(mainX, 64, 170, 24);
    addAndMakeVisible(*deviceTypeBox_);
    deviceTypeBox_->onChange = [this] {
        const int typeIdx = deviceTypeBox_->getSelectedId();
        const juce::OwnedArray<juce::AudioIODeviceType>& types = deviceManager_.getAvailableDeviceTypes();
        if (typeIdx > 0 && typeIdx <= types.size()) {
            host_.output().setMute(true);
            deviceManager_.setCurrentAudioDeviceType(types[typeIdx - 1]->getTypeName(), false);
            refreshAudioDeviceControls();
            pendingUnmute_ = juce::Time::getMillisecondCounter() + 2000;
        }
    };

    deviceBox_ = std::make_unique<juce::ComboBox>();
    deviceBox_->setBounds(mainX + 178, 64, 260, 24);
    addAndMakeVisible(*deviceBox_);

    sampleRateBox_ = std::make_unique<juce::ComboBox>();
    sampleRateBox_->setBounds(mainX + 446, 64, 90, 24);
    addAndMakeVisible(*sampleRateBox_);

    bufferSizeBox_ = std::make_unique<juce::ComboBox>();
    bufferSizeBox_->setBounds(mainX + 544, 64, 80, 24);
    addAndMakeVisible(*bufferSizeBox_);

    applyAudioButton_ = std::make_unique<juce::TextButton>("Apply");
    applyAudioButton_->setBounds(mainX + 632, 64, 52, 24);
    addAndMakeVisible(*applyAudioButton_);
    applyAudioButton_->onClick = [this] { applyAudioSetup(); };

    bypassToggle_ = std::make_unique<juce::ToggleButton>("Bypass");
    bypassToggle_->setBounds(mainX + 694, 64, 72, 24);
    bypassToggle_->setToggleState(false, juce::dontSendNotification);
    addAndMakeVisible(*bypassToggle_);
    bypassToggle_->onClick = [this] { host_.setBypass(bypassToggle_->getToggleState()); };

    // --- Tuner row ---
    addSectionLabel("TUNER", mainX, 98);
    tuningBox_ = std::make_unique<juce::ComboBox>();
    tuningBox_->setBounds(mainX, 116, 100, 24);
    addAndMakeVisible(*tuningBox_);
    tuningBox_->addItem("EADGBE", 1);
    tuningBox_->addItem("Drop D", 2);
    tuningBox_->setSelectedId(1, juce::dontSendNotification);
    tuningBox_->onChange = [this] {
        tuning_ = tuningBox_->getSelectedId() - 1;
        if (tuning_ < 0 || tuning_ >= kTuningCount) tuning_ = 0;
    };

    tunerMeter_ = std::make_unique<TunerMeter>();
    tunerMeter_->setBounds(mainX + 110, 114, 400, 28);
    addAndMakeVisible(*tunerMeter_);

    tunerLabel_ = std::make_unique<juce::Label>();
    tunerLabel_->setBounds(mainX + 518, 116, 460, 24);
    addAndMakeVisible(*tunerLabel_);

    tunerToggle_ = std::make_unique<juce::ToggleButton>("Tuner");
    tunerToggle_->setBounds(mainX, 146, 70, 22);
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
    sceneLabel_->setBounds(mainX + 80, 146, 300, 20);
    addAndMakeVisible(*sceneLabel_);

    // --- Add module row ---
    addSectionLabel("ADD MODULE", mainX, 178);
    addGroupBox_ = std::make_unique<juce::ComboBox>();
    addGroupBox_->setBounds(mainX, 196, 140, 24);
    addAndMakeVisible(*addGroupBox_);
    addGroupBox_->onChange = [this] { refreshModuleList(); };

    addModuleBox_ = std::make_unique<juce::ComboBox>();
    addModuleBox_->setBounds(mainX + 148, 196, 160, 24);
    addAndMakeVisible(*addModuleBox_);
    addModuleBox_->onChange = [this] { refreshAssetList(); };

    addAssetBox_ = std::make_unique<juce::ComboBox>();
    addAssetBox_->setBounds(mainX + 316, 196, 300, 24);
    addAndMakeVisible(*addAssetBox_);

    addModuleButton_ = std::make_unique<juce::TextButton>("Add");
    addModuleButton_->setBounds(mainX + 624, 196, 48, 24);
    addAndMakeVisible(*addModuleButton_);
    addModuleButton_->onClick = [this] {
        const int idx = addModuleBox_->getSelectedId();
        if (idx <= 0) return;
        std::string moduleId = addModuleBox_->getText().toStdString();
        std::string asset;
        if (juce::String(moduleId).startsWith("amp.")) {
            const int aidx = addAssetBox_->getSelectedId();
            if (aidx <= 0 || aidx > static_cast<int>(modelFiles_.size())) {
                statusLabel_->setText("select a model first", juce::dontSendNotification);
                return;
            }
            asset = modelFiles_[static_cast<std::size_t>(aidx - 1)].getFullPathName().toStdString();
        } else if (juce::String(moduleId).startsWith("cab.")) {
            const int aidx = addAssetBox_->getSelectedId();
            if (aidx > 0 && aidx <= static_cast<int>(irFiles_.size())) {
                asset = irFiles_[static_cast<std::size_t>(aidx - 1)].getFullPathName().toStdString();
            } else if (!irFiles_.empty()) {
                asset = irFiles_[0].getFullPathName().toStdString();
            } else {
                asset = demoDir_.getChildFile("irs/cab_clean.wav").getFullPathName().toStdString();
            }
        }
        std::string error;
        if (host_.addModuleToChain(moduleId, asset, error)) {
            refreshChainViews();
        } else {
            statusLabel_->setText("add failed: " + juce::String(error), juce::dontSendNotification);
        }
    };

    importModelButton_ = std::make_unique<juce::TextButton>("Import");
    importModelButton_->setBounds(mainX + 680, 196, 60, 24);
    addAndMakeVisible(*importModelButton_);
    importModelButton_->onClick = [this] {
#ifdef _WIN32
        char pathBuf[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "NAM models (*.nam)\0*.nam\0All files\0*.*\0\0";
        ofn.lpstrFile = pathBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = "Import NAM model";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameA(&ofn)) return;
        const juce::File src(ofn.lpstrFile);
#else
        return;
#endif
        const bool isIr = src.getFileExtension().equalsIgnoreCase(".wav");
        juce::File destDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                 .getChildFile("namfx")
                                 .getChildFile(isIr ? "irs" : "models");
        destDir.createDirectory();
        if (src.copyFileTo(destDir.getChildFile(src.getFileName()))) {
            refreshModelLibrary();
            refreshAssetList();
            statusLabel_->setText("imported " + src.getFileName(), juce::dontSendNotification);
        } else {
            statusLabel_->setText("import failed", juce::dontSendNotification);
        }
    };

    // --- Chain edit panel ---
    chainPanelViewport_ = std::make_unique<juce::Viewport>();
    chainPanelViewport_->setBounds(mainX, 230, mainW, 340);
    addAndMakeVisible(*chainPanelViewport_);
    chainPanelContent_ = std::make_unique<ChainPanelContent>();
    chainPanelViewport_->setViewedComponent(chainPanelContent_.get(), false);

    // --- Scene bar ---
    addSectionLabel("SCENE", mainX, 580);
    sceneBar_ = std::make_unique<juce::Component>();
    sceneBar_->setBounds(mainX, 598, 720, 26);
    addAndMakeVisible(*sceneBar_);

    sceneNameBox_ = std::make_unique<juce::TextEditor>();
    sceneNameBox_->setText("my_scene", false);
    sceneNameBox_->setBounds(mainX + 730, 598, 140, 26);
    addAndMakeVisible(*sceneNameBox_);

    sceneSaveButton_ = std::make_unique<juce::TextButton>("Save");
    sceneSaveButton_->setBounds(mainX + 876, 598, 48, 26);
    addAndMakeVisible(*sceneSaveButton_);
    sceneSaveButton_->onClick = [this] {
        std::string error;
        const int idx = (selectedScene_ >= 0) ? selectedScene_ : host_.sceneCount();
        if (host_.saveScene(idx, sceneNameBox_->getText().trim().toStdString(), error)) {
            statusLabel_->setText("scene saved", juce::dontSendNotification);
            rebuildSceneBar();
            refreshChainViews();
        } else {
            statusLabel_->setText("scene save failed: " + juce::String(error), juce::dontSendNotification);
        }
    };

    sceneLearnButton_ = std::make_unique<juce::TextButton>("Lrn CC");
    sceneLearnButton_->setBounds(mainX + 930, 598, 60, 26);
    addAndMakeVisible(*sceneLearnButton_);
    sceneLearnButton_->onClick = [this] {
        if (selectedScene_ < 0) {
            statusLabel_->setText("select a scene first", juce::dontSendNotification);
            return;
        }
        learningScene_ = selectedScene_ + 1;
        statusLabel_->setText(juce::String("learning: move a CC for Scene ") + juce::String(learningScene_), juce::dontSendNotification);
    };

    // --- MIDI ---
    midiBindLabel_ = std::make_unique<juce::Label>();
    midiBindLabel_->setBounds(mainX, 634, 900, 18);
    midiBindLabel_->setColour(juce::Label::textColourId, NAMTheme::textDim());
    addAndMakeVisible(*midiBindLabel_);
    midiBindLabel_->setText("no MIDI binds", juce::dontSendNotification);

    midiClearButton_ = std::make_unique<juce::TextButton>("Clear");
    midiClearButton_->setBounds(mainX + 920, 632, 48, 22);
    addAndMakeVisible(*midiClearButton_);
    midiClearButton_->onClick = [this] { clearMidiBinds(); };

    // --- Output ---
    addSectionLabel("OUTPUT", mainX, 662);
    auto makeSlider = [this, mainX](std::unique_ptr<juce::Slider>& s, int xOff, double min, double max, double value) {
        s = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        s->setRange(min, max, 0.1);
        s->setValue(value, juce::dontSendNotification);
        s->setBounds(mainX + xOff, 680, 140, 18);
        addAndMakeVisible(*s);
    };
    auto makeOutLabel = [this, mainX](const juce::String& text, int xOff) {
        auto* l = new juce::Label();
        l->setText(text, juce::dontSendNotification);
        l->setBounds(mainX + xOff, 678, 60, 20);
        addAndMakeVisible(l);
        outputLabels_.push_back(l);
    };

    makeOutLabel("Master", 0);
    makeSlider(masterSlider_, 62, -60.0, 0.0, 0.0);
    masterSlider_->onValueChange = [this] {
        host_.output().setMasterVolume(static_cast<float>(masterSlider_->getValue()));
        saveSettings();
    };

    makeOutLabel("InGain", 210);
    makeSlider(inputGainSlider_, 272, -60.0, 24.0, 0.0);
    inputGainSlider_->onValueChange = [this] {
        host_.output().setInputGain(static_cast<float>(inputGainSlider_->getValue()));
        saveSettings();
    };

    makeOutLabel("Bass", 420);
    makeSlider(bassSlider_, 482, 0.0, 1.0, 0.5);
    bassSlider_->onValueChange = [this] {
        host_.output().setBass(static_cast<float>(bassSlider_->getValue()));
        saveSettings();
    };

    makeOutLabel("Mid", 630);
    makeSlider(midSlider_, 692, 0.0, 1.0, 0.5);
    midSlider_->onValueChange = [this] {
        host_.output().setMiddle(static_cast<float>(midSlider_->getValue()));
        saveSettings();
    };

    makeOutLabel("Treble", 840);
    makeSlider(trebleSlider_, 902, 0.0, 1.0, 0.5);
    trebleSlider_->onValueChange = [this] {
        host_.output().setTreble(static_cast<float>(trebleSlider_->getValue()));
        saveSettings();
    };

    muteToggle_ = std::make_unique<juce::ToggleButton>("Mute");
    muteToggle_->setBounds(mainX + 1050, 678, 60, 22);
    addAndMakeVisible(*muteToggle_);
    muteToggle_->onClick = [this] {
        host_.output().setMute(muteToggle_->getToggleState());
        saveSettings();
    };

    levelInLabel_ = std::make_unique<juce::Label>();
    levelInLabel_->setBounds(mainX + 1120, 680, 100, 18);
    levelInLabel_->setColour(juce::Label::textColourId, NAMTheme::textDim());
    addAndMakeVisible(*levelInLabel_);
    levelOutLabel_ = std::make_unique<juce::Label>();
    levelOutLabel_->setBounds(mainX + 1220, 680, 100, 18);
    levelOutLabel_->setColour(juce::Label::textColourId, NAMTheme::textDim());
    addAndMakeVisible(*levelOutLabel_);

    // --- Chain summary ---
    chainLabel_ = std::make_unique<juce::TextEditor>();
    chainLabel_->setMultiLine(true);
    chainLabel_->setReadOnly(true);
    chainLabel_->setScrollbarsShown(true);
    chainLabel_->setBounds(mainX, 710, mainW, 170);
    addAndMakeVisible(*chainLabel_);
    chainLabel_->setText("(no preset loaded)", juce::dontSendNotification);

    refreshModelLibrary();
    rebuildAddModuleControls();
    resized();
}

void NAMEditorApplication::resized()
{
    if (presetBox_ == nullptr) {
        return;
    }
    const int width = getWidth();
    const int height = getHeight();
    const int sidebarW = juce::jlimit(236, 282, width / 5);
    const int mainX = sidebarW + 24;
    const int mainW = std::max(width - mainX - 18, 640);
    const int headerY = 16;

    if (presetListViewport_ != nullptr) {
        presetListViewport_->setBounds(18, 84, std::max(sidebarW - 36, 180),
                                       std::max(height - 170, 160));
        statusLabel_->setBounds(20, std::max(height - 72, 120), sidebarW - 40, 18);
        xrunLabel_->setBounds(20, std::max(height - 48, 140), sidebarW - 40, 18);
    }

    int x = mainX + 12;
    presetBox_->setBounds(x, headerY, 250, 34);
    x += 258;
    loadButton_->setBounds(x, headerY, 60, 34);
    x += 68;
    delPresetButton_->setBounds(x, headerY, 48, 34);
    x += 56;
    presetNameBox_->setBounds(x, headerY, 160, 34);
    x += 168;
    savePresetButton_->setBounds(x, headerY, 64, 34);

    const int audioY = 82;
    x = mainX + 12;
    deviceTypeBox_->setBounds(x, audioY, 126, 26);
    x += 134;
    deviceBox_->setBounds(x, audioY, 232, 26);
    x += 240;
    sampleRateBox_->setBounds(x, audioY, 86, 26);
    x += 94;
    bufferSizeBox_->setBounds(x, audioY, 82, 26);
    x += 90;
    applyAudioButton_->setBounds(x, audioY, 62, 26);
    x += 70;
    bypassToggle_->setBounds(x, audioY, 88, 26);

    const int tunerY = 116;
    tuningBox_->setBounds(mainX + 12, tunerY, 100, 26);
    tunerMeter_->setBounds(mainX + 122, tunerY + 1, 300, 24);
    const int tunerToggleX = mainX + mainW - 86;
    tunerToggle_->setBounds(tunerToggleX, tunerY, 82, 26);
    tunerLabel_->setBounds(mainX + 436, tunerY, std::max(tunerToggleX - mainX - 446, 150), 26);

    const int addY = 150;
    addGroupBox_->setBounds(mainX + 12, addY, 132, 26);
    addModuleBox_->setBounds(mainX + 152, addY, 156, 26);
    addAssetBox_->setBounds(mainX + 316, addY, std::max(mainW - 510, 150), 26);
    addModuleButton_->setBounds(mainX + mainW - 182, addY, 62, 26);
    importModelButton_->setBounds(mainX + mainW - 110, addY, 98, 26);

    const int chainY = 184;
    const int chainH = juce::jlimit(230, 360, height - 430);
    chainPanelViewport_->setBounds(mainX, chainY, mainW, chainH);
    if (chainPanelContent_ != nullptr) {
        chainPanelContent_->setSize(
            std::max(chainPanelContent_->getWidth(), std::max(mainW - 12, 720)),
            chainPanelContent_->getHeight());
    }

    const int bottomY = chainY + chainH + 12;
    const int sceneLabelY = bottomY;
    const int sceneY = bottomY + 18;
    const int sceneWidth = std::max(mainW - 500, 240);
    sceneBar_->setBounds(mainX + 12, sceneY, sceneWidth, 28);
    sceneNameBox_->setBounds(mainX + sceneWidth + 22, sceneY, 126, 28);
    sceneSaveButton_->setBounds(mainX + sceneWidth + 156, sceneY, 60, 28);
    sceneLearnButton_->setBounds(mainX + sceneWidth + 224, sceneY, 76, 28);
    sceneLabel_->setBounds(mainX + sceneWidth + 310, sceneY, std::max(mainW - sceneWidth - 322, 80), 28);
    midiBindLabel_->setBounds(mainX + 12, bottomY + 52, std::max(mainW - 100, 250), 20);
    midiClearButton_->setBounds(mainX + mainW - 76, bottomY + 50, 64, 24);

    const int outputY = bottomY + 82;
    const int outputStart = mainX + 12;
    const int outputWidth = std::max(mainW - 100, 500);
    const int blockW = std::max((outputWidth - 12) / 5, 92);
    juce::Slider* sliders[] = {masterSlider_.get(), inputGainSlider_.get(), bassSlider_.get(),
                               midSlider_.get(), trebleSlider_.get()};
    for (int i = 0; i < 5; ++i) {
        const int bx = outputStart + i * blockW;
        outputLabels_[static_cast<std::size_t>(i)]->setBounds(bx, outputY + 1, 40, 20);
        sliders[i]->setBounds(bx + 44, outputY + 1, std::max(blockW - 50, 42), 20);
    }
    muteToggle_->setBounds(mainX + mainW - 84, outputY - 1, 78, 26);
    levelInLabel_->setBounds(mainX + mainW - 170, outputY + 26, 78, 18);
    levelOutLabel_->setBounds(mainX + mainW - 84, outputY + 26, 78, 18);

    chainLabel_->setBounds(mainX, outputY + 48, mainW, std::max(height - outputY - 60, 40));

    for (juce::Label* label : sectionLabels_) {
        const juce::String text = label->getText();
        if (text == "PRESETS") {
            label->setBounds(20, 56, sidebarW - 40, 18);
        } else if (text == "AUDIO") {
            label->setBounds(mainX + 12, 64, 120, 14);
        } else if (text == "TUNER") {
            label->setBounds(mainX + 436, 102, 80, 14);
        } else if (text == "ADD MODULE") {
            label->setBounds(mainX + 12, 134, 140, 14);
        } else if (text == "SCENE") {
            label->setBounds(mainX + 12, sceneLabelY, 120, 14);
        } else if (text == "OUTPUT") {
            label->setBounds(mainX + 12, outputY - 18, 120, 14);
        }
    }
}

void NAMEditorApplication::paint(juce::Graphics& g)
{
    g.fillAll(NAMTheme::bg());
    const int sidebarW = juce::jlimit(236, 282, getWidth() / 5);
    const int mainX = sidebarW + 24;
    const int mainW = std::max(getWidth() - mainX - 18, 640);
    const int chainY = 184;
    const int chainH = juce::jlimit(230, 360, getHeight() - 430);
    const int bottomY = chainY + chainH + 12;

    auto panel = [&g](juce::Rectangle<int> bounds, juce::Colour colour, float radius) {
        g.setColour(colour);
        g.fillRoundedRectangle(bounds.toFloat(), radius);
        g.setColour(NAMTheme::panelBorder());
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), radius, 1.0f);
    };
    panel({12, 12, sidebarW - 24, getHeight() - 24}, NAMTheme::panel(), 10.0f);
    panel({mainX, 12, mainW, 52}, NAMTheme::panel(), 10.0f);
    panel({mainX, 70, mainW, 102}, NAMTheme::panel(), 10.0f);
    panel({mainX, chainY, mainW, chainH}, NAMTheme::panel(), 10.0f);
    panel({mainX, bottomY, mainW, std::max(getHeight() - bottomY - 12, 70)}, NAMTheme::panel(), 10.0f);

    g.setColour(NAMTheme::accent());
    g.fillRoundedRectangle(static_cast<float>(mainX + 14), 57.0f, 54.0f, 2.0f, 1.0f);
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f).withStyleFlags(juce::Font::bold)));
    g.setColour(NAMTheme::textBright());
    g.drawText("NAMFX / EDITOR", mainX + 14, 25, 180, 18, juce::Justification::left);
    g.setColour(NAMTheme::accent());
    g.fillEllipse(static_cast<float>(mainX + mainW - 86), 32.0f, 7.0f, 7.0f);
    g.setColour(NAMTheme::textDim());
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    g.drawText("AUDIO READY", mainX + mainW - 72, 26, 62, 18, juce::Justification::right);

    g.setColour(NAMTheme::textDim());
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyleFlags(juce::Font::bold)));
    g.drawText("DRIVER", mainX + 12, 73, 100, 12, juce::Justification::left);
    g.drawText("DEVICE", mainX + 146, 73, 120, 12, juce::Justification::left);
    g.drawText("RATE", mainX + 386, 73, 70, 12, juce::Justification::left);
    g.drawText("BUFFER", mainX + 476, 73, 70, 12, juce::Justification::left);
    g.drawText("TUNING", mainX + 12, 107, 100, 12, juce::Justification::left);
    g.drawText("SIGNAL CHAIN", mainX + 16, chainY + 10, 150, 14, juce::Justification::left);
}

} // namespace desktop
} // namespace namfx

// JUCE's application macro needs the class name at global scope; the crash
// handlers are installed in initialise() (process-wide)
START_JUCE_APPLICATION(namfx::desktop::NAMEditorApplication)
