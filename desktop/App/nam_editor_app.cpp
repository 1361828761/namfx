#include "desktop/App/nam_editor_app.h"

#include <juce_core/juce_core.h>

#include <csignal>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

// crash diagnostics (global scope: the custom main below needs qualified
// access): the headless test runs saw the app exit with code 3 (abort) and
// no WER record; log SEH/abort/lifecycle events to a file so the failure
// point is identifiable. Control-thread only.
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

} // namespace
MainWindow::MainWindow(juce::Component& content)
    : juce::DocumentWindow("NAM Editor", juce::Colours::darkgrey,
                           juce::DocumentWindow::allButtons)
{
    setContentNonOwned(&content, false);
    setResizable(true, false);
    setUsingNativeTitleBar(true);
    centreWithSize(content.getWidth(), content.getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
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
    demoDir_ = juce::File::getCurrentWorkingDirectory().getChildFile("core/preset/demo");
    buildUi();
    mainWindow_ = std::make_unique<MainWindow>(asComponent());
    // default audio output setup: 48 kHz, 256 samples
    setAudioChannels(2, 2);
    startTimer(kTimerMs);
    rebuildPresetList();
    crashLog("lifecycle: initialise done");
}

void NAMEditorApplication::shutdown()
{
    crashLog("lifecycle: shutdown");
    shutdownAudio();
}

void NAMEditorApplication::systemRequestedQuit()
{
    quit();
}

void NAMEditorApplication::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    // scratch buffers are sized here (device/block changes), never inside
    // the callback
    zeroIn_.assign(static_cast<std::size_t>(samplesPerBlockExpected), 0.0f);
    scratchL_.resize(static_cast<std::size_t>(samplesPerBlockExpected));
    scratchR_.resize(static_cast<std::size_t>(samplesPerBlockExpected));
    host_.prepare(sampleRate, samplesPerBlockExpected);
}

void NAMEditorApplication::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    const int n = bufferToFill.numSamples;
    juce::AudioBuffer<float>& buf = *bufferToFill.buffer;
    const int numCh = buf.getNumChannels();
    // Channel-safe: mono devices deliver a 1-channel buffer; never index
    // past the real channel count (JUCE Debug asserts crash otherwise).
    const float* inL = numCh > 0 ? buf.getReadPointer(0, bufferToFill.startSample)
                                 : zeroIn_.data();
    const float* inR = numCh > 1 ? buf.getReadPointer(1, bufferToFill.startSample)
                                 : zeroIn_.data();
    if (numCh > 1) {
        float* outL = buf.getWritePointer(0, bufferToFill.startSample);
        float* outR = buf.getWritePointer(1, bufferToFill.startSample);
        host_.process(inL, inR, outL, outR, n);
    } else if (numCh == 1) {
        float* outL = buf.getWritePointer(0, bufferToFill.startSample);
        host_.process(inL, zeroIn_.data(), scratchL_.data(), scratchR_.data(), n);
        for (int i = 0; i < n; ++i) {
            outL[i] = scratchL_[static_cast<std::size_t>(i)];
        }
    } else {
        host_.process(zeroIn_.data(), zeroIn_.data(), scratchL_.data(), scratchR_.data(), n);
    }
}

void NAMEditorApplication::releaseResources()
{
}

void NAMEditorApplication::timerCallback()
{
    if (++aliveTicks_ % 50 == 0) {
        crashLog("alive tick"); // 5 s heartbeat for crash diagnostics
    }
    const dsp::Tuner& tuner = host_.tuner();
    if (tuner.noteDetected()) {
        tunerLabel_->setText("Tuner: " + juce::String(tuner.frequency(), 1) + " Hz, "
                                 + juce::String(tuner.midiNote()) + " ("
                                 + juce::String(tuner.cents(), 1) + " ct)",
                             juce::dontSendNotification);
    } else {
        tunerLabel_->setText("Tuner: --", juce::dontSendNotification);
    }
    if (host_.sceneCount() > 0) {
        sceneLabel_->setText("Scene: " + juce::String(host_.activeScene() + 1) + "/"
                                 + juce::String(host_.sceneCount()),
                             juce::dontSendNotification);
    }
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

void NAMEditorApplication::buildUi()
{
    setSize(640, 360);
    presetBox_ = std::make_unique<juce::ComboBox>();
    presetBox_->setBounds(16, 16, 280, 28);
    addAndMakeVisible(*presetBox_);
    presetBox_->onChange = [this] { loadSelectedPreset(); };

    loadButton_ = std::make_unique<juce::TextButton>("Load");
    loadButton_->setBounds(304, 16, 72, 28);
    addAndMakeVisible(*loadButton_);
    loadButton_->onClick = [this] { loadSelectedPreset(); };

    statusLabel_ = std::make_unique<juce::Label>();
    statusLabel_->setBounds(16, 56, 600, 24);
    addAndMakeVisible(*statusLabel_);

    tunerLabel_ = std::make_unique<juce::Label>();
    tunerLabel_->setBounds(16, 88, 320, 24);
    addAndMakeVisible(*tunerLabel_);

    sceneLabel_ = std::make_unique<juce::Label>();
    sceneLabel_->setBounds(340, 88, 280, 24);
    addAndMakeVisible(*sceneLabel_);

    // chain view: what is actually loaded (module per slot + parameter
    // values), read from the engine after a load
    chainLabel_ = std::make_unique<juce::TextEditor>();
    chainLabel_->setMultiLine(true);
    chainLabel_->setReadOnly(true);
    chainLabel_->setScrollbarsShown(true);
    chainLabel_->setBounds(16, 120, 600, 210);
    addAndMakeVisible(*chainLabel_);
    chainLabel_->setText("(no preset loaded)", juce::dontSendNotification);
}

} // namespace desktop
} // namespace namfx

// JUCE's application macro needs the class name at global scope; the crash
// handlers are installed in initialise() (process-wide)
START_JUCE_APPLICATION(namfx::desktop::NAMEditorApplication)
