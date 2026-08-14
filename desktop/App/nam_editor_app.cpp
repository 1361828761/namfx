#include "desktop/App/nam_editor_app.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iterator>

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
    if (t.contains("amp") || t.contains("preamp")) {
        return "Amp";
    }
    if (t.contains("cab")) {
        return "Cab";
    }
    const juce::String n = name.toLowerCase();
    if (n.contains("cab") || n.contains("ir")) {
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
    demoDir_ = juce::File(NAMFX_DEMO_DIR);
    userPresetDir_ = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile("namfx")
                         .getChildFile("presets");
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
    // load a default preset so the chain is never empty at startup
    juce::File defaultPreset = demoDir_.getChildFile("clean.json");
    if (!defaultPreset.exists()) {
        defaultPreset = demoDir_.getChildFile("boost.json");
    }
    if (defaultPreset.exists()) {
        loadPresetFile(defaultPreset);
    }
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
}

void NAMEditorApplication::loadPresetFile(const juce::File& file)
{
    std::string error;
    const bool ok = host_.loadPreset(file.getFullPathName().toStdString(),
                                     demoDir_.getFullPathName().toStdString(), error);
    if (ok) {
        statusLabel_->setText("loaded " + file.getFileName(), juce::dontSendNotification);
        refreshChainViews();
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

void NAMEditorApplication::rebuildAddModuleControls()
{
    // module categories map from the module-id prefix
    struct Group {
        const char* name;
        const char* prefix;
    };
    static const Group kGroups[] = {
        {"Amp (NAM)", "amp."},        {"Cabinet", "cab."},
        {"Distortion", "od."},        {"Compressor", "comp."},
        {"Noise Gate", "gate."},      {"Modulation", "mod."},
        {"Delay", "dly."},            {"Reverb", "rvb."},
        {"Pitch", "pitch."},          {"EQ", "eq."},
        {"Gain / Tone", ""},
    };
    const std::vector<std::string> ids = host_.moduleIds();

    addGroupBox_->clear(juce::dontSendNotification);
    for (int g = 0; g < static_cast<int>(std::size(kGroups)); ++g) {
        bool has = false;
        for (const std::string& id : ids) {
            if (juce::String(id).startsWith(kGroups[g].prefix)) {
                has = true;
                break;
            }
        }
        if (has) {
            addGroupBox_->addItem(kGroups[g].name, g + 1);
        }
    }
    if (addGroupBox_->getNumItems() > 0) {
        addGroupBox_->setSelectedId(1, juce::dontSendNotification);
    }
    refreshModuleList();
}

void NAMEditorApplication::refreshModuleList()
{
    struct Group {
        const char* name;
        const char* prefix;
    };
    static const Group kGroups[] = {
        {"Amp (NAM)", "amp."},        {"Cabinet", "cab."},
        {"Distortion", "od."},        {"Compressor", "comp."},
        {"Noise Gate", "gate."},      {"Modulation", "mod."},
        {"Delay", "dly."},            {"Reverb", "rvb."},
        {"Pitch", "pitch."},          {"EQ", "eq."},
        {"Gain / Tone", ""},
    };
    const int g = addGroupBox_->getSelectedId() - 1;
    const std::vector<std::string> ids = host_.moduleIds();

    addModuleBox_->clear(juce::dontSendNotification);
    if (g < 0 || g >= static_cast<int>(std::size(kGroups))) {
        return;
    }
    for (const std::string& id : ids) {
        if (juce::String(id).startsWith(kGroups[g].prefix)) {
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
    addAssetBox_->clear(juce::dontSendNotification);
    const juce::String sel = addModuleBox_->getText();
    const bool needsAsset = sel.startsWith("amp.");
    addAssetBox_->setVisible(needsAsset);
    if (!needsAsset) {
        return;
    }
    // grouped by type + brand with section headings
    juce::String currentGroup;
    for (std::size_t i = 0; i < modelFiles_.size(); ++i) {
        const ModelClass c = classifyModel(modelFiles_[i]);
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

void NAMEditorApplication::addSectionLabel(const juce::String& text, int y)
{
    auto* l = new juce::Label();
    l->setText(text, juce::dontSendNotification);
    l->setFont(juce::Font(juce::FontOptions().withHeight(11.0f).withStyleFlags(
        juce::Font::bold)));
    l->setColour(juce::Label::textColourId, NAMTheme::textDim());
    l->setBounds(16, y, 200, 14);
    addAndMakeVisible(l);
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
    content_.setDropIndexAt(p.getY());
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

void ChainPanelContent::setDropIndexAt(int y)
{
    int row = 0;
    for (int i = 0; i < static_cast<int>(rowBounds_.size()); ++i) {
        if (y < rowBounds_[static_cast<std::size_t>(i)]) {
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
    g.fillAll(NAMTheme::panel());
    if (dropIndex_ >= 0 && dropIndex_ < static_cast<int>(rowBounds_.size())) {
        // insertion indicator between rows
        const int y = rowBounds_[static_cast<std::size_t>(dropIndex_)] - 2;
        g.setColour(NAMTheme::accent());
        g.fillRect(4, y, getWidth() - 8, 2);
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

void NAMEditorApplication::rebuildChainPanel()
{
    chainPanelContent_->removeAllChildren();
    const std::vector<EngineHost::SlotInfo> infos = host_.chainInfo();
    int y = 4;
    std::vector<int> bounds;
    for (const EngineHost::SlotInfo& info : infos) {
        auto* grip = new GripLabel(info.slot, *this, *chainPanelContent_);
        grip->setBounds(4, y + 1, 18, 18);
        chainPanelContent_->addAndMakeVisible(grip);

        auto* name = new juce::Label();
        name->setText(juce::String(info.slot) + "  " + info.moduleId, juce::dontSendNotification);
        name->setBounds(26, y, 150, 20);
        chainPanelContent_->addAndMakeVisible(name);

        auto* up = new juce::TextButton("^");
        up->setBounds(164, y, 22, 20);
        chainPanelContent_->addAndMakeVisible(up);
        up->onClick = [this, slot = info.slot] {
            std::string error;
            host_.moveModule(slot, -1, error);
            refreshChainViews();
        };

        auto* dn = new juce::TextButton("v");
        dn->setBounds(188, y, 22, 20);
        chainPanelContent_->addAndMakeVisible(dn);
        dn->onClick = [this, slot = info.slot] {
            std::string error;
            host_.moveModule(slot, 1, error);
            refreshChainViews();
        };

        auto* bp = new juce::ToggleButton("Byp");
        bp->setToggleState(info.bypass, juce::dontSendNotification);
        bp->setBounds(214, y, 44, 20);
        chainPanelContent_->addAndMakeVisible(bp);
        bp->onClick = [this, slot = info.slot, bp] {
            host_.uiSetBypass(slot, bp->getToggleState());
        };

        int py = y + 22;
        for (std::size_t p = 0; p < info.specs.size(); ++p) {
            auto* pl = new juce::Label();
            pl->setText(info.specs[p].displayName.empty() ? info.specs[p].id
                                                          : info.specs[p].displayName,
                        juce::dontSendNotification);
            pl->setBounds(8, py, 110, 18);
            chainPanelContent_->addAndMakeVisible(pl);

            auto* sl = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
            sl->setRange(info.specs[p].min, info.specs[p].max, 0.001);
            sl->setValue(info.values[p], juce::dontSendNotification);
            sl->setBounds(122, py, 240, 18);
            chainPanelContent_->addAndMakeVisible(sl);

            auto* vl = new juce::Label();
            vl->setText(juce::String(info.values[p], 2), juce::dontSendNotification);
            vl->setBounds(368, py, 90, 18);
            chainPanelContent_->addAndMakeVisible(vl);

            const int slot = info.slot;
            const std::string paramId = info.specs[p].id;
            sl->onValueChange = [this, sl, vl, slot, paramId] {
                host_.uiSetParam(slot, paramId, static_cast<float>(sl->getValue()));
                vl->setText(juce::String(sl->getValue(), 2), juce::dontSendNotification);
            };
            py += 22;
        }

        auto* del = new juce::TextButton("Del");
        del->setBounds(466, y, 40, 20);
        chainPanelContent_->addAndMakeVisible(del);
        del->onClick = [this, slot = info.slot] {
            std::string error;
            host_.removeModuleFromChain(slot, error);
            refreshChainViews();
        };

        y = py + 6;
        bounds.push_back(y);
    }
    chainPanelContent_->setSize(1220, std::max(y, 200));
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
    setSize(1280, 720);

    addSectionLabel("PRESET", 8);
    presetBox_ = std::make_unique<juce::ComboBox>();
    presetBox_->setBounds(16, 26, 360, 28);
    addAndMakeVisible(*presetBox_);
    presetBox_->onChange = [this] { loadSelectedPreset(); };

    loadButton_ = std::make_unique<juce::TextButton>("Load");
    loadButton_->setBounds(384, 26, 72, 28);
    addAndMakeVisible(*loadButton_);
    loadButton_->onClick = [this] { loadSelectedPreset(); };

    // save the current chain as a user preset (up to 100)
    presetNameBox_ = std::make_unique<juce::TextEditor>();
    presetNameBox_->setText("my_tone", false);
    presetNameBox_->setBounds(464, 26, 160, 28);
    addAndMakeVisible(*presetNameBox_);

    savePresetButton_ = std::make_unique<juce::TextButton>("Save");
    savePresetButton_->setBounds(630, 26, 56, 28);
    addAndMakeVisible(*savePresetButton_);
    savePresetButton_->onClick = [this] { saveUserPreset(); };

    statusLabel_ = std::make_unique<juce::Label>();
    statusLabel_->setBounds(470, 30, 560, 20);
    addAndMakeVisible(*statusLabel_);

    xrunLabel_ = std::make_unique<juce::Label>();
    xrunLabel_->setBounds(1040, 30, 200, 20);
    addAndMakeVisible(*xrunLabel_);

    addSectionLabel("AUDIO", 62);
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

    // master bypass: clean input passthrough (whole engine skipped)
    bypassToggle_ = std::make_unique<juce::ToggleButton>("Bypass");
    bypassToggle_->setBounds(856, 80, 80, 26);
    bypassToggle_->setToggleState(false, juce::dontSendNotification);
    addAndMakeVisible(*bypassToggle_);
    bypassToggle_->onClick = [this] {
        host_.setBypass(bypassToggle_->getToggleState());
    };

    addSectionLabel("TUNER", 116);
    // tuner panel: tuning selection + graphical deviation meter
    tuningBox_ = std::make_unique<juce::ComboBox>();
    tuningBox_->setBounds(16, 134, 120, 26);
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
    tunerMeter_->setBounds(144, 130, 480, 34);
    addAndMakeVisible(*tunerMeter_);

    tunerLabel_ = std::make_unique<juce::Label>();
    tunerLabel_->setBounds(636, 134, 620, 24);
    addAndMakeVisible(*tunerLabel_);

    tunerToggle_ = std::make_unique<juce::ToggleButton>("Tuner");
    tunerToggle_->setBounds(16, 168, 88, 24);
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
    sceneLabel_->setBounds(120, 168, 400, 20);
    addAndMakeVisible(*sceneLabel_);

    addSectionLabel("CHAIN", 200);
    // add-module row: category -> module -> asset (NAM model / IR)
    addGroupBox_ = std::make_unique<juce::ComboBox>();
    addGroupBox_->setBounds(16, 218, 150, 26);
    addAndMakeVisible(*addGroupBox_);
    addGroupBox_->onChange = [this] { refreshModuleList(); };

    addModuleBox_ = std::make_unique<juce::ComboBox>();
    addModuleBox_->setBounds(174, 218, 170, 26);
    addAndMakeVisible(*addModuleBox_);
    addModuleBox_->onChange = [this] { refreshAssetList(); };

    addAssetBox_ = std::make_unique<juce::ComboBox>();
    addAssetBox_->setBounds(352, 218, 360, 26);
    addAndMakeVisible(*addAssetBox_);

    addModuleButton_ = std::make_unique<juce::TextButton>("Add");
    addModuleButton_->setBounds(720, 218, 56, 26);
    addAndMakeVisible(*addModuleButton_);
    addModuleButton_->onClick = [this] {
        const int idx = addModuleBox_->getSelectedId();
        if (idx <= 0) {
            return;
        }
        std::string moduleId = addModuleBox_->getText().toStdString();
        std::string asset;
        if (juce::String(moduleId).startsWith("amp.")) {
            const int aidx = addAssetBox_->getSelectedId();
            if (aidx <= 0 || aidx > static_cast<int>(modelFiles_.size())) {
                statusLabel_->setText("select a model first", juce::dontSendNotification);
                return;
            }
            asset = modelFiles_[static_cast<std::size_t>(aidx - 1)].getFullPathName()
                        .toStdString();
        } else if (juce::String(moduleId).startsWith("cab.")) {
            // cabinet modules use the bundled demo IR by default
            asset = demoDir_.getChildFile("irs/cab_clean.wav").getFullPathName().toStdString();
        }
        std::string error;
        if (host_.addModuleToChain(moduleId, asset, error)) {
            refreshChainViews();
        } else {
            statusLabel_->setText("add failed: " + juce::String(error),
                                  juce::dontSendNotification);
        }
    };

    // chain edit panel: one block per slot (name, bypass, per-parameter
    // sliders, delete), scrollable
    chainPanelViewport_ = std::make_unique<juce::Viewport>();
    chainPanelViewport_->setBounds(16, 252, 1240, 250);
    addAndMakeVisible(*chainPanelViewport_);
    chainPanelContent_ = std::make_unique<ChainPanelContent>();
    chainPanelViewport_->setViewedComponent(chainPanelContent_.get(), false);

    addSectionLabel("OUTPUT", 510);
    // output panel: master / input gain / 3-band EQ / mute
    auto makeSlider = [this](std::unique_ptr<juce::Slider>& s, int x, double min, double max,
                             double value) {
        s = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                           juce::Slider::NoTextBox);
        s->setRange(min, max, 0.1);
        s->setValue(value, juce::dontSendNotification);
        s->setBounds(x, 530, 130, 18);
        addAndMakeVisible(*s);
    };
    auto makeOutLabel = [this](const juce::String& text, int x) {
        auto* l = new juce::Label();
        l->setText(text, juce::dontSendNotification);
        l->setBounds(x, 528, 60, 20);
        addAndMakeVisible(l);
    };

    makeOutLabel("Master", 8);
    makeSlider(masterSlider_, 70, -60.0, 0.0, 0.0);
    masterSlider_->onValueChange = [this] {
        host_.output().setMasterVolume(static_cast<float>(masterSlider_->getValue()));
    };

    makeOutLabel("InGain", 210);
    makeSlider(inputGainSlider_, 272, -60.0, 24.0, 0.0);
    inputGainSlider_->onValueChange = [this] {
        host_.output().setInputGain(static_cast<float>(inputGainSlider_->getValue()));
    };

    makeOutLabel("Bass", 412);
    makeSlider(bassSlider_, 460, 0.0, 1.0, 0.5);
    bassSlider_->onValueChange = [this] {
        host_.output().setBass(static_cast<float>(bassSlider_->getValue()));
    };

    makeOutLabel("Mid", 600);
    makeSlider(midSlider_, 648, 0.0, 1.0, 0.5);
    midSlider_->onValueChange = [this] {
        host_.output().setMiddle(static_cast<float>(midSlider_->getValue()));
    };

    makeOutLabel("Treble", 788);
    makeSlider(trebleSlider_, 836, 0.0, 1.0, 0.5);
    trebleSlider_->onValueChange = [this] {
        host_.output().setTreble(static_cast<float>(trebleSlider_->getValue()));
    };

    muteToggle_ = std::make_unique<juce::ToggleButton>("Mute");
    muteToggle_->setBounds(976, 526, 60, 22);
    addAndMakeVisible(*muteToggle_);
    muteToggle_->onClick = [this] {
        host_.output().setMute(muteToggle_->getToggleState());
    };

    // chain view: read-only summary of what is actually loaded
    chainLabel_ = std::make_unique<juce::TextEditor>();
    chainLabel_->setMultiLine(true);
    chainLabel_->setReadOnly(true);
    chainLabel_->setScrollbarsShown(true);
    chainLabel_->setBounds(16, 560, 1240, 140);
    addAndMakeVisible(*chainLabel_);
    chainLabel_->setText("(no preset loaded)", juce::dontSendNotification);

    refreshModelLibrary();
    rebuildAddModuleControls();
}

void NAMEditorApplication::paint(juce::Graphics& g)
{
    g.fillAll(NAMTheme::bg());
}

} // namespace desktop
} // namespace namfx

// JUCE's application macro needs the class name at global scope; the crash
// handlers are installed in initialise() (process-wide)
START_JUCE_APPLICATION(namfx::desktop::NAMEditorApplication)
