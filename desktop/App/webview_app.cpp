// NAMFX WebView2 shell — the WebUI as the product GUI, with a native audio
// backend (WASAPI exclusive/shared + ASIO via JUCE) driving the same
// EngineHost the web frontend talks to over localhost HTTP.
//
// Red lines: JUCE confined to desktop/ (this target lives here); the audio
// callback path is exactly the editor's (EngineAudioSource), the HTTP
// control plane reuses webui/server/web_host.cpp (zero JUCE) via the
// embedded WebHost with an externally owned EngineHost.

#include "desktop/Engine/engine_host.h"
#include "desktop/Engine/native_audio_backend.h"
#include "webui/server/http_server.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <filesystem>
#include <memory>
#include <string>

namespace namfx {

using desktop::EngineHost;

namespace web {

class WebHost;
HttpHandler makeHandler(WebHost& host);
WebHost* webHostCreate(EngineHost* host, const char* wwwDir, const char* demoDir,
                       AudioBackend* audioBackend);
void webHostStartTicker(WebHost& host);
void webHostStopTicker(WebHost& host);
void webHostStopClients(WebHost& host);
void webHostDestroy(WebHost* host);

} // namespace web

namespace {

constexpr int kPort = 8812;

#ifndef NAMFX_WEBUI_DIR
#define NAMFX_WEBUI_DIR "webui/www"
#endif
#ifndef NAMFX_DEMO_DIR
#define NAMFX_DEMO_DIR "core/preset/demo"
#endif

// packaged builds ship www/ + presets-demo/ next to the exe; prefer those
// over the compile-time absolute source-tree paths
std::string packagedDir(const char* subDir, const char* fallback)
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0) {
        const std::wstring exe(buf, n);
        const std::size_t slash = exe.find_last_of(L"/\\");
        const std::wstring dir = slash == std::wstring::npos ? L"" : exe.substr(0, slash);
        const std::wstring candidate = dir + L"\\" + std::wstring(subDir, subDir + std::strlen(subDir));
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec)) {
            return juce::String(candidate.c_str()).toStdString();
        }
    }
#else
    (void)subDir;
#endif
    return std::string(fallback);
}

} // namespace

// ---------------------------------------------------------------------------

class MainShellWindow : public juce::DocumentWindow {
public:
    explicit MainShellWindow(int port)
        : juce::DocumentWindow("NAMFX", juce::Colour(0xff070a0e),
                               juce::DocumentWindow::allButtons, true)
    {
        setUsingNativeTitleBar(true);
        const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/?wasm=0";

        juce::WebBrowserComponent::Options options;
        options = options.withBackend(juce::WebBrowserComponent::Options::Backend::webview2);
        browser_ = std::make_unique<juce::WebBrowserComponent>(options);
        setContentOwned(browser_.get(), false);
        setResizable(true, true);
        setSize(1280, 840);
        centreWithSize(1280, 840);
        setVisible(true);
        browser_->goToURL(juce::String(url));
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    std::unique_ptr<juce::WebBrowserComponent> browser_;
};

// ---------------------------------------------------------------------------

class WebViewShellApplication : public juce::JUCEApplication {
public:
    WebViewShellApplication() : audio_(host_) {}

    const juce::String getApplicationName() override { return "NAMFX WebUI"; }
    const juce::String getApplicationVersion() override { return "0.2.0-web"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        std::string audioError;
        audio_.initialize(audioError);

        startHttpServer();
        if (server_ == nullptr) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "NAMFX",
                                                   "WebUI 服务无法启动，请关闭占用 8812/8813 端口的进程后重试。");
            quit();
            return;
        }

        window_ = std::make_unique<MainShellWindow>(httpPort_);
    }

    void shutdown() override
    {
        window_ = nullptr;
        stopHttpServer();
        audio_.shutdown();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    void startHttpServer()
    {
        if (server_ != nullptr) {
            return;
        }
        const std::string wwwDir = packagedDir("www", NAMFX_WEBUI_DIR);
        const std::string demoDir = packagedDir("presets-demo", NAMFX_DEMO_DIR);
        webHost_ = namfx::web::webHostCreate(&host_, wwwDir.c_str(), demoDir.c_str(), &audio_);
        server_ = std::make_unique<namfx::web::HttpServer>(
            namfx::web::makeHandler(*webHost_));
        httpPort_ = kPort;
        if (!server_->start("127.0.0.1", httpPort_)) {
            server_ = nullptr;
            namfx::web::webHostDestroy(webHost_);
            webHost_ = namfx::web::webHostCreate(&host_, wwwDir.c_str(), demoDir.c_str(), &audio_);
            server_ = std::make_unique<namfx::web::HttpServer>(
                namfx::web::makeHandler(*webHost_));
            httpPort_ = kPort + 1;
            if (!server_->start("127.0.0.1", httpPort_)) {
                server_ = nullptr;
                namfx::web::webHostDestroy(webHost_);
                webHost_ = nullptr;
                return;
            }
        }
        namfx::web::webHostStartTicker(*webHost_);
    }

    void stopHttpServer()
    {
        if (server_ == nullptr) {
            return;
        }
        // 1) unblock SSE connection threads (they sleep-wait on client liveness)
        if (webHost_ != nullptr) {
            namfx::web::webHostStopClients(*webHost_);
        }
        // 2) stop the listener and wait for every connection thread to exit
        server_->stop();
        if (webHost_ != nullptr) {
            namfx::web::webHostStopTicker(*webHost_);
            namfx::web::webHostDestroy(webHost_);
            webHost_ = nullptr;
        }
        server_ = nullptr;
    }

    EngineHost host_;
    desktop::NativeAudioBackend audio_;

    int httpPort_ = kPort;
    namfx::web::WebHost* webHost_ = nullptr;
    std::unique_ptr<namfx::web::HttpServer> server_;

    std::unique_ptr<MainShellWindow> window_;
};

} // namespace namfx

START_JUCE_APPLICATION(namfx::WebViewShellApplication)
