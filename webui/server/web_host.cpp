// NAMFX WebUI host — HTTP + SSE control plane over EngineHost.
// Shell code (not core): undo/redo + A/B buffers live here (PLAN D8),
// the audio engine stays the parameter authority.
//
// Usage: namfx_web [--port N] [--bind IP] [--www DIR] [--demo-dir DIR]
//
// The WebHost class is also embedded by the WebView2 shell (JUCE desktop):
// define NAMFX_WEB_EMBEDDED to drop the standalone main(), and construct
// WebHost with an externally owned EngineHost (native audio callback).

#include "desktop/Engine/engine_host.h"
#include "webui/server/audio_backend.h"
#include "webui/server/http_server.h"

#ifndef NAMFX_WEB_EMBEDDED
#include "desktop/Engine/native_audio_backend.h"
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

#ifndef NAMFX_WEBUI_DIR
#define NAMFX_WEBUI_DIR "webui/www"
#endif
#ifndef NAMFX_DEMO_DIR
#define NAMFX_DEMO_DIR "core/preset/demo"
#endif
#ifndef NAMFX_NAM_DIR
#define NAMFX_NAM_DIR "nam"
#endif
#ifndef NAMFX_MODELS_DIR
#define NAMFX_MODELS_DIR "modles"
#endif
#ifndef NAMFX_IR_DIR
#define NAMFX_IR_DIR "ir"
#endif

namespace namfx {
namespace web {

using nlohmann::json;
using desktop::EngineHost; // EngineHost lives in the desktop shell namespace

namespace {

constexpr int kPort = 8810;
constexpr std::size_t kMaxBody = 64 * 1024 * 1024; // import cap (64 MB)

std::string userDataDir()
{
#ifdef _WIN32
    char* profile = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&profile, &len, "USERPROFILE") == 0 && profile != nullptr) {
        const std::string out = std::string(profile) + "\\Documents\\namfx";
        std::free(profile);
        return out;
    }
#endif
    const char* home = std::getenv("HOME");
    return home != nullptr ? std::string(home) + "/.namfx" : ".namfx";
}

bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const std::string& path, const std::string& data)
{
    // atomic write protocol (docs/EXECUTION.md 已知坑): temp -> flush ->
    // rename; never leave a half-written file at the target path
    const std::string tmp = path + ".tmp";
#ifdef _WIN32
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) {
        return false;
    }
    const std::size_t written = std::fwrite(data.data(), 1, data.size(), f);
    const int commitResult = _commit(_fileno(f));
    std::fclose(f);
    if (written != data.size() || commitResult != 0) {
        std::remove(tmp.c_str());
        return false;
    }
#else
    std::ofstream f(tmp, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.close();
    if (!f) {
        std::remove(tmp.c_str());
        return false;
    }
#endif
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool ensureDir(const std::string& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

std::string joinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) {
        return b;
    }
    const char sep = a.find('/') != std::string::npos ? '/' : '\\';
    if (a.back() == '/' || a.back() == '\\') {
        return a + b;
    }
    return a + sep + b;
}

std::string baseName(const std::string& p)
{
    const std::size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

std::string stripExtension(const std::string& p)
{
    const std::size_t dot = p.find_last_of('.');
    return dot == std::string::npos ? p : p.substr(0, dot);
}

bool dirExists(const std::string& p)
{
    std::error_code ec;
    return std::filesystem::is_directory(p, ec);
}

// ---------- preset listing ----------

struct PresetEntry {
    std::string file;   // filename
    std::string name;
    std::string label;
    std::string scope;  // demo | user
};

std::string presetLabel(const std::string& file, int index)
{
    std::smatch m;
    static const std::regex rx(R"(^(\d{2})([A-C])[-_])");
    if (std::regex_search(file, m, rx) && m.size() == 3) {
        return m[1].str() + m[2].str();
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d", index / 3 + 1);
    return std::string(buf) + "ABC"[index % 3];
}

int demoPresetRank(const std::string& file)
{
    static const char* kOrder[] = {
        "clean.json", "boost.json", "bright.json", "mellow.json", "warm.json",
        "ts_drive.json", "transparent.json", "mosfet_drive.json", "ota_comp.json",
        "chorus.json", "flanger.json", "phaser.json", "wah.json", "gate.json",
        "eq.json", "delay.json", "tape.json", "spring.json", "hall.json",
        "pitch.json", "octave.json", "cab.json", "nam_amp.json", "chain_drive.json",
    };
    for (int i = 0; i < static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0])); ++i) {
        if (file == kOrder[i]) {
            return i;
        }
    }
    return 10000;
}

std::string presetNameFromFile(const std::string& path)
{
    std::string text;
    if (readFile(path, text)) {
        try {
            const json j = json::parse(text);
            if (j.contains("name") && j["name"].is_string()) {
                return j["name"].get<std::string>();
            }
        } catch (...) {
        }
    }
    return stripExtension(baseName(path));
}

void scanPresetDir(const std::string& dir, const std::string& scope,
                   std::vector<PresetEntry>& out)
{
    if (!dirExists(dir)) {
        return;
    }
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    if (scope == "demo") {
        std::stable_sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
            const int ar = demoPresetRank(baseName(a));
            const int br = demoPresetRank(baseName(b));
            return ar == br ? a < b : ar < br;
        });
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        PresetEntry e;
        e.file = baseName(files[i]);
        e.name = presetNameFromFile(files[i]);
        e.label = presetLabel(e.file, static_cast<int>(i));
        e.scope = scope;
        out.push_back(std::move(e));
    }
}

// ---------- NAM model classification (ported from the JUCE editor) ----------

std::string extractJsonString(const std::string& head, const std::string& key)
{
    const std::string pat = "\"" + key + "\"";
    const std::size_t p = head.find(pat);
    if (p == std::string::npos) {
        return {};
    }
    const std::size_t c = head.find(':', p + pat.size());
    if (c == std::string::npos) {
        return {};
    }
    const std::size_t q1 = head.find('"', c + 1);
    if (q1 == std::string::npos) {
        return {};
    }
    const std::size_t q2 = head.find('"', q1 + 1);
    if (q2 == std::string::npos) {
        return {};
    }
    return head.substr(q1 + 1, q2 - q1 - 1);
}

std::string lower(const std::string& s)
{
    std::string out = s;
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

bool contains(const std::string& hay, const std::string& needle)
{
    return hay.find(needle) != std::string::npos;
}

std::string modelTypeFor(const std::string& gearType, const std::string& name)
{
    const std::string t = lower(gearType);
    if (contains(t, "amp_cab") || t == "ampcab" || (contains(t, "amp") && contains(t, "cab"))) {
        return "Amp+Cab";
    }
    if (contains(t, "amp") || contains(t, "preamp")) {
        return "Amp";
    }
    if (contains(t, "cab")) {
        return "Cab";
    }
    const std::string n = lower(name);
    if (contains(n, "cab") || contains(n, "ir") || contains(n, "412") || contains(n, "212")) {
        return "Cab";
    }
    if (contains(n, "amp") || contains(n, "head")) {
        return "Amp";
    }
    return "Pedal";
}

std::string modelBrandFor(const std::string& make, const std::string& name)
{
    const std::string all = lower(make + " " + name);
    if (contains(all, "ac30") || contains(all, "vox")) {
        return "Vox";
    }
    if (contains(all, "marshall") || contains(all, "mars") || contains(all, "plexi")
        || contains(all, "jcm") || contains(all, "1959")) {
        return "Marshall";
    }
    if (contains(all, "fender") || contains(all, "deluxe") || contains(all, "twin")
        || contains(all, "princeton") || contains(all, "reverb")) {
        return "Fender";
    }
    if (contains(all, "ocd")) {
        return "Fulltone";
    }
    if (contains(all, "centaur") || contains(all, "klon")) {
        return "Klon";
    }
    if (contains(all, "rat")) {
        return "ProCo";
    }
    if (contains(all, "od3") || contains(all, "boss")) {
        return "Boss";
    }
    if (contains(all, "bno") || contains(all, "jt45") || contains(all, "jtm")) {
        return "BNO";
    }
    if (contains(all, "5150")) {
        return "EVH";
    }
    const std::size_t sp = make.find(' ');
    return sp == std::string::npos && !make.empty() ? make : "Other";
}

struct ModelInfo {
    std::string name;
    std::string type;
    std::string brand;
    std::string file; // full path
};

} // namespace

struct SSEClient {
    std::mutex mu;
    bool alive = true;
    StreamWriter write;
};

// ---------- host shell ----------

class WebHost {
public:
    // host is owned by the caller (JUCE shell / standalone); when null the
    // standalone path creates and prepares its own engine
    WebHost(EngineHost* host, std::string wwwDir, std::string demoDir,
            AudioBackend* audioBackend = nullptr)
        : wwwDir_(std::move(wwwDir)), demoDir_(std::move(demoDir)), audioBackend_(audioBackend)
    {
        ownedHost_ = host == nullptr ? std::make_unique<EngineHost>() : nullptr;
        host_ = host != nullptr ? host : ownedHost_.get();
        userDir_ = userDataDir();
        userPresetDir_ = joinPath(userDir_, "presets");
        userModelDir_ = joinPath(userDir_, "models");
        userIrDir_ = joinPath(userDir_, "irs");
        if (ownedHost_) {
            host_->prepare(48000.0, 128);
        }
        rescan();
        // default preset so the chain is never empty
        if (!presets_[0].empty()) {
            std::string error;
            host_->loadPreset(joinPath(demoDir_, presets_[0][0].file), demoDir_, error);
            current_ = presets_[0][0];
            resetChainLayout();
        }
    }

    // ---- state snapshot ----
    json snapshot(bool tickOnly) const
    {
        json j = json::object();
        if (tickOnly) {
            j["levels"] = levelsJson();
            j["tuner"] = tunerJson();
            j["perf"] = perfJson();
            return j;
        }
        j["presets"] = presetsJson();
        j["currentPreset"] = currentJson();
        j["chain"] = chainJson();
        j["chainLayout"] = chainLayout_;
        j["scenes"] = scenesJson();
        j["activeScene"] = host_->activeScene();
        j["dirty"] = dirty_;
        j["output"] = outputJson();
        j["levels"] = levelsJson();
        j["tuner"] = tunerJson();
        j["perf"] = perfJson();
        j["midi"] = midiJson();
        j["ab"] = abJson();
        j["undo"] = undoJson();
        j["library"] = libraryJson();
        const AudioBackendState audio = audioBackend_ != nullptr
                                            ? audioBackend_->state()
                                            : AudioBackendState{};
        json audioTypes = json::array();
        for (const AudioDeviceTypeInfo& type : audio.types) {
            audioTypes.push_back({{"name", type.name}, {"devices", type.devices}});
        }
        j["engine"] = {
            {"sampleRate", audio.active ? audio.sampleRate : 48000},
            {"blockSize", audio.active ? audio.blockSize : 128},
            {"audio", audio.active ? json(audio.type) : json(false)},
            {"audioType", audio.type},
            {"audioDevice", audio.device},
            {"audioTypes", audioTypes},
            {"audioError", audio.error},
            {"version", "0.1.0-web"},
        };
        j["msg"] = msg_;
        j["msgErr"] = msgErr_;
        j["connected"] = true;
        j["statusText"] = "已连接";
        return j;
    }

    // ---- command dispatch (shell mutex held by caller) ----
    json dispatch(const json& cmd)
    {
        const std::string name = cmd.value("cmd", "");
        json res = json::object();
        std::string error;
        const auto ok = [&res]() { res["ok"] = true; return res; };
        const auto fail = [&res](const std::string& e) {
            res["ok"] = false;
            res["error"] = e;
            return res;
        };
        try {
            if (name == "loadPreset") {
                const std::string scope = cmd.value("scope", "demo");
                const std::string file = cmd.value("file", "");
                const std::string dir = scope == "user" ? userPresetDir_ : demoDir_;
                std::vector<PresetEntry>* list = &presets_[0];
                if (scope == "user") {
                    list = &presets_[1];
                }
                auto it = std::find_if(list->begin(), list->end(),
                                       [&](const PresetEntry& e) { return e.file == file; });
                if (it == list->end()) {
                    return fail("未找到预设 " + file);
                }
                if (!host_->loadPreset(joinPath(dir, file), dir, error)) {
                    return fail("预设加载失败: " + error);
                }
                resetChainLayout();
                current_ = *it;
                dirty_ = false;
                abA_.clear();
                abB_.clear();
                msg_ = "已加载预设 " + it->name;
                undoStack_.clear();
                redoStack_.clear();
                pushUndo();
                return ok();
            }
            if (name == "savePreset") {
                const std::string nm = cmd.value("name", "");
                if (nm.empty() || nm.find_first_of("/\\.") != std::string::npos) {
                    return fail("预设名不合法（不允许 / \\ .）");
                }
                if (cmd.contains("chainJson")) {
                    const std::string text = cmd["chainJson"].get<std::string>();
                    if (!host_->loadPresetText(text, userPresetDir_, error)) {
                        return fail("导入预设失败: " + error);
                    }
                    resetChainLayout();
                }
                // fixed-slot model: the ABC label decides the filename
                // (e.g. "09A"); saving to a slot replaces whatever user
                // preset currently occupies it
                std::string label = cmd.value("label", "");
                if (!label.empty()) {
                    const bool valid = label.size() >= 3 && std::isdigit(static_cast<unsigned char>(label[0]))
                        && std::isdigit(static_cast<unsigned char>(label[1]))
                        && (label[2] == 'A' || label[2] == 'B' || label[2] == 'C');
                    if (!valid) {
                        return fail("槽位不合法（如 09A）");
                    }
                    for (const PresetEntry& x : presets_[1]) {
                        if (x.label == label) {
                            std::remove(joinPath(userPresetDir_, x.file).c_str());
                        }
                    }
                    rescan();
                }
                if (!host_->savePreset(nm, userPresetDir_, error)) {
                    return fail("保存失败: " + error);
                }
                if (!label.empty()) {
                    const std::string from = joinPath(userPresetDir_, nm + ".json");
                    const std::string to = joinPath(userPresetDir_, label + "_" + nm + ".json");
                    std::error_code ec;
                    std::filesystem::rename(from, to, ec);
                    if (ec) {
                        std::remove(from.c_str());
                        return fail("槽位写入失败");
                    }
                }
                rescan();
                PresetEntry e;
                e.file = (label.empty() ? nm : label + "_" + nm) + ".json";
                e.name = nm;
                e.scope = "user";
                auto it = std::find_if(presets_[1].begin(), presets_[1].end(),
                                       [&](const PresetEntry& x) { return x.file == e.file; });
                e.label = it == presets_[1].end()
                              ? presetLabel(e.file, static_cast<int>(presets_[1].size()))
                              : it->label;
                current_ = e;
                dirty_ = false;
                msg_ = "已保存预设 " + nm + (label.empty() ? "" : " → " + label);
                return ok();
            }
            if (name == "loadEmpty") {
                const std::string empty = "{\"schema\":1,\"name\":\"empty\",\"chain\":[],\"scenes\":[]}";
                if (!host_->loadPresetText(empty, demoDir_, error)) {
                    return fail("清空失败: " + error);
                }
                resetChainLayout();
                current_.file.clear();
                dirty_ = false;
                msg_ = "已清空效果链（空预设槽）";
                return ok();
            }
            if (name == "deletePreset") {
                const std::string file = cmd.value("file", "");
                const std::string scope = cmd.value("scope", "user");
                if (scope == "demo") {
                    return fail("出厂预设不可删除");
                }
                const std::string path = joinPath(userPresetDir_, file);
                if (std::remove(path.c_str()) != 0) {
                    return fail("删除失败");
                }
                rescan();
                if (current_.file == file && current_.scope == "user") {
                    // deleting the active preset empties the chain
                    // (fixed-slot behavior: the slot becomes a blank slate)
                    const std::string empty = "{\"schema\":1,\"name\":\"empty\",\"chain\":[],\"scenes\":[]}";
                    host_->loadPresetText(empty, demoDir_, error);
                    resetChainLayout();
                    current_.file.clear();
                }
                msg_ = "已删除预设 " + file;
                return ok();
            }
            if (name == "exportPreset") {
                res["ok"] = true;
                res["text"] = host_->exportPresetJson(current_.name.empty() ? "preset" : current_.name);
                return res;
            }
            if (name == "addModule" || name == "insertModule") {
                const std::string moduleId = cmd.value("moduleId", "");
                std::string asset = cmd.value("asset", "");
                if (!asset.empty()) {
                    asset = resolveAsset(asset, moduleId);
                    if (asset.empty()) {
                        return fail("资产未找到（先导入模型/IR）");
                    }
                }
                bool okFlag = false;
                if (name == "insertModule") {
                    const int idx = cmd.value("index", -1);
                    okFlag = host_->insertModuleToChain(idx < 0 ? 0 : idx, moduleId, asset, error);
                } else {
                    okFlag = host_->addModuleToChain(moduleId, asset, error);
                }
                if (!okFlag) {
                    return fail("添加失败: " + error);
                }
                resetChainLayout();
                dirty_ = true;
                msg_ = "已添加 " + moduleId;
                pushUndo();
                return ok();
            }
            if (name == "removeModule") {
                if (!host_->removeModuleFromChain(cmd.value("slot", -1), error)) {
                    return fail("移除失败: " + error);
                }
                resetChainLayout();
                dirty_ = true;
                msg_ = "已移除槽位";
                pushUndo();
                return ok();
            }
            if (name == "moveModule") {
                if (!host_->moveModule(cmd.value("slot", -1), cmd.value("direction", 0), error)) {
                    return fail("移动失败: " + error);
                }
                resetChainLayout();
                dirty_ = true;
                pushUndo();
                return ok();
            }
            if (name == "moveModuleTo") {
                if (!host_->moveModuleTo(cmd.value("slot", -1), cmd.value("index", 0), error)) {
                    return fail("移动失败: " + error);
                }
                if (cmd.contains("visualSource") && cmd.contains("visualTarget")) {
                    moveChainLayout(cmd.value("visualSource", -1), cmd.value("visualTarget", -1));
                } else {
                    resetChainLayout();
                }
                dirty_ = true;
                pushUndo();
                return ok();
            }
            if (name == "swapModule") {
                if (!host_->swapModules(cmd.value("slot", -1), cmd.value("target", -1), error)) {
                    return fail("交换失败: " + error);
                }
                if (cmd.contains("visualSource") && cmd.contains("visualTarget")) {
                    swapChainLayout(cmd.value("visualSource", -1), cmd.value("visualTarget", -1));
                } else {
                    resetChainLayout();
                }
                dirty_ = true;
                pushUndo();
                return ok();
            }
            if (name == "setParam") {
                if (!host_->uiSetParam(cmd.value("slot", -1), cmd.value("param", ""),
                                       static_cast<float>(cmd.value("value", 0.0)))) {
                    return fail("参数写入失败");
                }
                dirty_ = true;
                return ok();
            }
            if (name == "setBypass") {
                if (!host_->uiSetBypass(cmd.value("slot", -1), cmd.value("bypass", false))) {
                    return fail("旁路写入失败");
                }
                dirty_ = true;
                return ok();
            }
            if (name == "setMix") {
                if (!host_->uiSetMix(cmd.value("slot", -1),
                                     static_cast<float>(cmd.value("mix", 1.0)))) {
                    return fail("干湿写入失败");
                }
                dirty_ = true;
                return ok();
            }
            if (name == "recallScene") {
                host_->recallScene(cmd.value("index", 0));
                dirty_ = true;
                msg_ = "已切换场景";
                pushUndo();
                return ok();
            }
            if (name == "saveScene") {
                if (!host_->saveScene(cmd.value("index", -1), cmd.value("name", ""), error)) {
                    return fail("场景存储失败: " + error);
                }
                msg_ = "已存储场景";
                pushUndo();
                return ok();
            }
            if (name == "setOutput") {
                const std::string key = cmd.value("key", "");
                if (key == "master") {
                    const double v = cmd.value("value", 0.0);
                    host_->output().setMasterVolume(static_cast<float>(v));
                    output_["master"] = v;
                } else if (key == "ingain") {
                    const double v = cmd.value("value", 0.0);
                    host_->output().setInputGain(static_cast<float>(v));
                    output_["ingain"] = v;
                } else if (key == "bass") {
                    const double v = cmd.value("value", 0.5);
                    host_->output().setBass(static_cast<float>(v));
                    output_["bass"] = v;
                } else if (key == "mid") {
                    const double v = cmd.value("value", 0.5);
                    host_->output().setMiddle(static_cast<float>(v));
                    output_["mid"] = v;
                } else if (key == "treble") {
                    const double v = cmd.value("value", 0.5);
                    host_->output().setTreble(static_cast<float>(v));
                    output_["treble"] = v;
                } else if (key == "lowcut") {
                    const double v = cmd.value("value", 20.0);
                    host_->output().setLowCut(static_cast<float>(v));
                    output_["lowcut"] = v;
                } else if (key == "highcut") {
                    const double v = cmd.value("value", 20000.0);
                    host_->output().setHighCut(static_cast<float>(v));
                    output_["highcut"] = v;
                } else if (key == "mute") {
                    const bool value = cmd.value("value", false);
                    host_->output().setMute(value);
                    output_["mute"] = value;
                    msg_ = value ? "已静音" : "已取消静音";
                } else if (key == "masterBypass") {
                    const bool value = cmd.value("value", false);
                    host_->setBypass(value);
                    output_["masterBypass"] = value;
                    msg_ = value ? "总旁路开启（干音直通）" : "总旁路关闭";
                } else {
                    return fail("未知输出参数 " + key);
                }
                return ok();
            }
            if (name == "setAudio") {
                if (audioBackend_ == nullptr) {
                    return fail("当前宿主没有原生音频后端");
                }
                const std::string type = cmd.value("type", "");
                const std::string device = cmd.value("device", "");
                const double sampleRate = cmd.value("sampleRate", 44100.0);
                const int blockSize = cmd.value("blockSize", 128);
                if (!audioBackend_->apply(type, device, sampleRate, blockSize, error)) {
                    return fail("音频设备打开失败: " + error);
                }
                msg_ = "已连接 " + type + " / " + device;
                return ok();
            }
            if (name == "setTunerOn") {
                tunerOn_ = cmd.value("on", true);
                return ok();
            }
            if (name == "setTuning") {
                tuning_ = cmd.value("tuning", 0) == 1 ? 1 : 0;
                return ok();
            }
            if (name == "learnParam") {
                learning_ = {true, "param", cmd.value("module", ""), cmd.value("param", ""), 0};
                msg_ = "学习中：等待 CC 输入";
                return ok();
            }
            if (name == "learnScene") {
                learning_ = {true, "scene", "", "", cmd.value("index", 0)};
                msg_ = "学习中：等待 CC 输入";
                return ok();
            }
            if (name == "learnCancel") {
                learning_.active = false;
                msg_ = "已取消学习";
                return ok();
            }
            if (name == "midiLearnParam") {
                if (!learning_.active || learning_.kind != "param") {
                    return fail("未处于参数学习态");
                }
                if (!host_->midiLearnParam(cmd.value("cc", -1), learning_.module, learning_.param,
                                           error)) {
                    return fail("绑定失败: " + error);
                }
                learning_.active = false;
                msg_ = "已学习 CC " + std::to_string(cmd.value("cc", 0));
                return ok();
            }
            if (name == "midiLearnScene") {
                if (!learning_.active || learning_.kind != "scene") {
                    return fail("未处于场景学习态");
                }
                if (!host_->midiLearnScene(cmd.value("cc", -1), learning_.sceneIndex + 1, error)) {
                    return fail("绑定失败: " + error);
                }
                learning_.active = false;
                msg_ = "已学习 CC " + std::to_string(cmd.value("cc", 0));
                return ok();
            }
            if (name == "midiClear") {
                for (const auto& b : host_->midiBindings()) {
                    host_->midiClearBind(b.cc);
                }
                msg_ = "已清空 MIDI 绑定";
                return ok();
            }
            if (name == "undo") {
                if (undoStack_.size() <= 1) {
                    return fail("没有可撤销的操作");
                }
                redoStack_.push_back(undoStack_.back());
                undoStack_.pop_back();
                if (!host_->loadPresetText(undoStack_.back(), demoDir_, error)) {
                    undoStack_.push_back(redoStack_.back());
                    redoStack_.pop_back();
                    return fail("撤销失败: " + error);
                }
                resetChainLayout();
                dirty_ = false;
                return ok();
            }
            if (name == "redo") {
                if (redoStack_.empty()) {
                    return fail("没有可重做的操作");
                }
                undoStack_.push_back(redoStack_.back());
                redoStack_.pop_back();
                if (!host_->loadPresetText(undoStack_.back(), demoDir_, error)) {
                    redoStack_.push_back(undoStack_.back());
                    undoStack_.pop_back();
                    return fail("重做失败: " + error);
                }
                resetChainLayout();
                dirty_ = false;
                return ok();
            }
            if (name == "copyToA") {
                abA_ = host_->exportPresetJson("A");
                msg_ = "已复制当前音色到 A";
                return ok();
            }
            if (name == "copyToB") {
                abB_ = host_->exportPresetJson("B");
                msg_ = "已复制当前音色到 B";
                return ok();
            }
            if (name == "applyA" || name == "applyB") {
                const std::string& buf = name == "applyA" ? abA_ : abB_;
                if (buf.empty()) {
                    return fail(name == "applyA" ? "A 缓冲为空" : "B 缓冲为空");
                }
                if (!host_->loadPresetText(buf, demoDir_, error)) {
                    return fail("应用失败: " + error);
                }
                resetChainLayout();
                msg_ = "已切换到 " + name.substr(5) + " 音色";
                dirty_ = true;
                return ok();
            }
            if (name == "importModel" || name == "importIr") {
                return fail("导入请用 PUT /api/import");
            }
            if (name == "setLocked") {
                return ok(); // 前端本地态
            }
            return fail("未知命令 " + name);
        } catch (const std::exception& e) {
            return fail(std::string("命令异常: ") + e.what());
        }
    }

    // PUT /api/import?name=xxx.nam, body = raw file bytes
    json importFile(const std::string& name, const std::string& body)
    {
        if (name.empty() || name.find("..") != std::string::npos
            || name.find_first_of("/\\") != std::string::npos) {
            return {{"ok", false}, {"error", "文件名不合法"}};
        }
        const bool isNam = contains(lower(name), ".nam");
        const bool isIr = contains(lower(name), ".wav");
        if (!isNam && !isIr) {
            return {{"ok", false}, {"error", "仅支持 .nam / .wav"}};
        }
        const std::string dir = isNam ? userModelDir_ : userIrDir_;
        ensureDir(dir);
        const std::string path = joinPath(dir, name);
        if (!writeFile(path, body)) {
            return {{"ok", false}, {"error", "写入失败"}};
        }
        rescanLibrary();
        msg_ = "已导入 " + name;
        return {{"ok", true}};
    }

    // ---- SSE broadcast ----
    void broadcast(const json& j, bool tick)
    {
        std::string payload;
        try {
            payload = "data: " + j.dump() + "\n\n";
        } catch (...) {
            return;
        }
        std::lock_guard<std::mutex> lock(evMu_);
        for (auto it = clients_.begin(); it != clients_.end();) {
            const auto& c = *it;
            std::lock_guard<std::mutex> wlock(c->mu);
            if (!c->write(payload)) {
                c->alive = false;
                it = clients_.erase(it);
            } else {
                ++it;
            }
        }
        (void)tick;
    }

    std::shared_ptr<SSEClient> addClient(const StreamWriter& write)
    {
        auto c = std::make_shared<SSEClient>();
        c->write = write;
        std::lock_guard<std::mutex> lock(evMu_);
        clients_.push_back(c);
        return c;
    }

    void removeClient(const std::shared_ptr<SSEClient>& c)
    {
        std::lock_guard<std::mutex> lock(evMu_);
        c->alive = false;
        clients_.erase(std::remove(clients_.begin(), clients_.end(), c), clients_.end());
    }

    // mark every SSE client dead so their connection threads exit; the
    // shell calls this before destroying the host / stopping the server
    void stopClients()
    {
        std::lock_guard<std::mutex> lock(evMu_);
        for (const auto& c : clients_) {
            c->alive = false;
        }
    }

    // ---- background tick: levels / tuner / perf at 10 Hz ----
    void startTicker()
    {
        ticker_ = std::thread([this] {
            while (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (audioBackend_ != nullptr) {
                    audioBackend_->tick();
                }
                json j = json::object();
                j["tick"] = true;
                j["state"] = snapshot(true);
                broadcast(j, true);
            }
        });
    }

    // ---- audio pump: null backend has no callback, but param smoothing,
    // bypass fades and scene ramps only advance inside Chain::process();
    // feed zero blocks at realtime pace so the control plane sees live values.
    // The WebView2 shell has a real audio callback and must NOT call this.
    void startAudioPump()
    {
        pump_ = std::thread([this] {
            constexpr int kBlock = 128;
            std::vector<float> inL(kBlock, 0.0f), inR(kBlock, 0.0f);
            std::vector<float> outL(kBlock, 0.0f), outR(kBlock, 0.0f);
            while (running_.load()) {
                host_->process(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
                std::this_thread::sleep_for(std::chrono::microseconds(2667)); // 128 @ 48k
            }
        });
    }

    void stopTicker()
    {
        running_ = false;
        if (ticker_.joinable()) {
            ticker_.join();
        }
    }

    // ---- accessors ----
    const std::string& wwwDir() const { return wwwDir_; }
    const std::string& demoDir() const { return demoDir_; }
    const std::string& userPresetDir() const { return userPresetDir_; }
    bool hasAudioBackend() const
    {
        return audioBackend_ != nullptr && audioBackend_->state().active;
    }
    std::mutex& shellMutex() { return shellMutex_; }
    void setDirty(bool dirty) { dirty_ = dirty; }

    // MIDI learning completion (called from the MIDI input thread)
    void onMidiCc(int cc)
    {
        std::lock_guard<std::mutex> lock(shellMutex_);
        if (!learning_.active) {
            return;
        }
        std::string error;
        if (learning_.kind == "param") {
            if (host_->midiLearnParam(cc, learning_.module, learning_.param, error)) {
                learning_.active = false;
                msg_ = "已学习 CC " + std::to_string(cc);
            }
        } else if (host_->midiLearnScene(cc, learning_.sceneIndex + 1, error)) {
            learning_.active = false;
            msg_ = "已学习 CC " + std::to_string(cc);
        }
    }

private:
    void resetChainLayout()
    {
        // layout holds ENGINE slot ids (-1 = empty visual slot); slot ids
        // are unique even for repeated module instances, unlike module ids
        chainLayout_.assign(12, -1);
        for (const auto& info : host_->chainInfo()) {
            if (info.slot >= 0 && info.slot < static_cast<int>(chainLayout_.size())) {
                chainLayout_[static_cast<std::size_t>(info.slot)] = info.slot;
            }
        }
    }

    void moveChainLayout(int source, int target)
    {
        if (source < 0 || target < 0 || source >= static_cast<int>(chainLayout_.size())
            || target >= static_cast<int>(chainLayout_.size()) || source == target
            || chainLayout_[source] < 0 || chainLayout_[target] >= 0) {
            resetChainLayout();
            return;
        }
        chainLayout_[target] = chainLayout_[source];
        chainLayout_[source] = -1;
    }

    void swapChainLayout(int source, int target)
    {
        if (source < 0 || target < 0 || source >= static_cast<int>(chainLayout_.size())
            || target >= static_cast<int>(chainLayout_.size()) || source == target
            || chainLayout_[source] < 0 || chainLayout_[target] < 0) {
            resetChainLayout();
            return;
        }
        std::swap(chainLayout_[source], chainLayout_[target]);
    }

    void rescan()
    {
        presets_[0].clear();
        presets_[1].clear();
        scanPresetDir(demoDir_, "demo", presets_[0]);
        ensureDir(userPresetDir_);
        scanPresetDir(userPresetDir_, "user", presets_[1]);
        for (std::size_t i = 0; i < presets_[1].size(); ++i) {
            presets_[1][i].label = presetLabel(presets_[1][i].file,
                                               static_cast<int>(presets_[0].size() + i));
        }
        rescanLibrary();
    }

    void rescanLibrary()
    {
        models_.clear();
        irs_.clear();
        const char* exeDir = exeAdjacentDir();
        std::vector<std::string> modelDirs = {userModelDir_};
        if (exeDir != nullptr) {
            modelDirs.push_back(joinPath(exeDir, "models"));
        }
        modelDirs.push_back(joinPath(demoDir_, "models"));
        modelDirs.push_back(joinPath(NAMFX_MODELS_DIR, "nam"));
        modelDirs.push_back(NAMFX_NAM_DIR);
        std::set<std::string> seenModelNames;
        for (const std::string& d : modelDirs) {
            if (!dirExists(d)) {
                continue;
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(d)) {
                if (entry.is_regular_file()
                    && lower(entry.path().extension().string()) == ".nam") {
                    ModelInfo m;
                    m.file = entry.path().string();
                    m.name = stripExtension(baseName(m.file));
                    std::string head;
                    if (readFile(m.file, head) && head.size() > 65536) {
                        head.resize(65536);
                    }
                    if (!seenModelNames.insert(lower(baseName(m.file))).second) {
                        continue;
                    }
                    const std::string gearMake = extractJsonString(head, "gear_make");
                    const std::string jsonName = extractJsonString(head, "name");
                    if (!jsonName.empty()) {
                        m.name = jsonName;
                    }
                    if (!gearMake.empty() || !jsonName.empty()) {
                        if (m.name.empty()) {
                            m.name = stripExtension(baseName(m.file));
                        }
                        m.type = modelTypeFor(extractJsonString(head, "gear_type"), m.name);
                        m.brand = modelBrandFor(gearMake, m.name);
                    } else {
                        m.type = modelTypeFor("", m.name);
                        m.brand = modelBrandFor("", m.name);
                    }
                    models_.push_back(std::move(m));
                }
            }
        }
        std::vector<std::string> irDirs = {userIrDir_};
        if (exeDir != nullptr) {
            irDirs.push_back(joinPath(exeDir, "irs"));
        }
        irDirs.push_back(joinPath(demoDir_, "irs"));
        irDirs.push_back(joinPath(NAMFX_MODELS_DIR, "ir"));
        irDirs.push_back(NAMFX_IR_DIR);
        std::set<std::string> seenIrNames;
        for (const std::string& d : irDirs) {
            if (!dirExists(d)) {
                continue;
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(d)) {
                if (entry.is_regular_file()
                    && lower(entry.path().extension().string()) == ".wav") {
                    const std::string path = entry.path().string();
                    if (seenIrNames.insert(lower(baseName(path))).second) {
                        irs_.push_back(path);
                    }
                }
            }
        }
        std::sort(irs_.begin(), irs_.end());
        std::sort(models_.begin(), models_.end(),
                  [](const ModelInfo& a, const ModelInfo& b) {
                      if (a.type != b.type) {
                          return a.type < b.type;
                      }
                      if (a.brand != b.brand) {
                          return a.brand < b.brand;
                      }
                      return a.name < b.name;
                  });
    }

    static const char* exeAdjacentDir()
    {
        static std::string dir;
        if (!dir.empty()) {
            return dir.c_str();
        }
#ifdef _WIN32
        char buf[MAX_PATH];
        const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n > 0) {
            const std::string exe(buf, n);
            const std::size_t slash = exe.find_last_of("/\\");
            dir = slash == std::string::npos ? exe : exe.substr(0, slash);
            return dir.c_str();
        }
#endif
        return nullptr;
    }

    std::string resolveAsset(const std::string& name, const std::string& moduleId) const
    {
        const bool wantNam = moduleId == "amp.nam";
        const std::vector<std::string>& files = wantNam ? modelFiles() : irFiles();
        const std::string ext = wantNam ? ".nam" : ".wav";
        for (const std::string& f : files) {
            if (stripExtension(baseName(f)) == name || baseName(f) == name
                || stripExtension(baseName(f)) + ext == name + ext) {
                return f;
            }
        }
        return {};
    }

    const std::vector<std::string>& modelFiles() const
    {
        // models_ holds ModelInfo; flatten on demand
        modelPaths_.clear();
        for (const ModelInfo& m : models_) {
            modelPaths_.push_back(m.file);
        }
        return modelPaths_;
    }
    const std::vector<std::string>& irFiles() const
    {
        irPaths_.clear();
        irPaths_ = irs_;
        return irPaths_;
    }

    // ---- json builders ----
    json presetsJson() const
    {
        json j = json::object();
        j["demo"] = json::array();
        j["user"] = json::array();
        for (const auto& e : presets_[0]) {
            j["demo"].push_back({{"file", e.file}, {"name", e.name}, {"label", e.label}});
        }
        for (const auto& e : presets_[1]) {
            j["user"].push_back({{"file", e.file}, {"name", e.name}, {"label", e.label}});
        }
        return j;
    }

    json currentJson() const
    {
        if (current_.file.empty()) {
            return nullptr;
        }
        return json{{"file", current_.file}, {"name", current_.name},
                    {"label", current_.label}, {"scope", current_.scope}};
    }

    json chainJson() const
    {
        json arr = json::array();
        for (const auto& info : host_->chainInfo()) {
            json slot = json::object();
            slot["slot"] = info.slot;
            slot["module"] = info.moduleId;
            slot["assetName"] = info.assetName;
            slot["bypass"] = info.bypass;
            slot["mix"] = info.mix;
            json specs = json::array();
            json params = json::object();
            for (std::size_t i = 0; i < info.specs.size(); ++i) {
                const ParamSpec& sp = info.specs[i];
                specs.push_back({{"id", sp.id}, {"name", sp.displayName}, {"min", sp.min},
                                 {"max", sp.max}, {"def", sp.defaultValue}, {"unit", sp.unit},
                                 {"taper", sp.taper == Taper::Log ? "log" : "linear"}});
                params[sp.id] = i < info.values.size() ? info.values[i] : sp.defaultValue;
            }
            slot["specs"] = specs;
            slot["params"] = params;
            arr.push_back(std::move(slot));
        }
        return arr;
    }

    json scenesJson() const
    {
        json arr = json::array();
        for (const auto& sc : host_->sceneDefs()) {
            json ovs = json::array();
            for (const auto& ov : sc.overrides) {
                json p = json::object();
                for (const auto& pi : ov.params) {
                    p[pi.id] = pi.value;
                }
                ovs.push_back({{"moduleId", ov.moduleId}, {"bypass", ov.bypass}, {"params", p}});
            }
            arr.push_back({{"name", sc.name}, {"overrides", ovs}});
        }
        return arr;
    }

    json outputJson() const
    {
        json j = json::object();
        const auto getNumber = [this](const char* key, double def) {
            const auto it = output_.find(key);
            return it != output_.end() && it->is_number() ? it->get<double>() : def;
        };
        const auto getBool = [this](const char* key, bool def) {
            const auto it = output_.find(key);
            if (it == output_.end()) {
                return def;
            }
            if (it->is_boolean()) {
                return it->get<bool>();
            }
            return it->is_number() ? it->get<double>() > 0.5 : def;
        };
        j["master"] = getNumber("master", 0.0);
        j["ingain"] = getNumber("ingain", 0.0);
        j["bass"] = getNumber("bass", 0.5);
        j["mid"] = getNumber("mid", 0.5);
        j["treble"] = getNumber("treble", 0.5);
        j["lowcut"] = getNumber("lowcut", 20.0);
        j["highcut"] = getNumber("highcut", 20000.0);
        j["mute"] = getBool("mute", false);
        j["masterBypass"] = getBool("masterBypass", false);
        return j;
    }

    json levelsJson() const
    {
        return json{{"in", host_->inputLevel()}, {"out", host_->outputLevel()}};
    }

    json tunerJson() const
    {
        const dsp::Tuner& t = host_->tuner();
        json j = json::object();
        j["on"] = tunerOn_;
        j["tuning"] = tuning_;
        j["detected"] = t.noteDetected();
        j["freq"] = t.frequency();
        j["cents"] = t.cents();
        j["signal"] = false;
        if (!t.noteDetected()) {
            j["note"] = "";
            j["string"] = "";
            j["target"] = 0;
            j["inTune"] = false;
        } else {
            static const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            static const int kTargets[2][6] = {{64, 59, 55, 50, 45, 40}, {64, 59, 55, 50, 45, 38}};
            static const char* kStrings[6] = {"1stE4", "2ndB3", "3rdG3", "4thD3", "5thA2", "6thE2"};
            const int note = t.midiNote();
            int best = 0;
            int bestDist = 99;
            for (int i = 0; i < 6; ++i) {
                const int d = std::abs(note - kTargets[tuning_][i]);
                if (d < bestDist) {
                    bestDist = d;
                    best = i;
                }
            }
            char noteBuf[8];
            std::snprintf(noteBuf, sizeof(noteBuf), "%s%d", kNames[note % 12], note / 12 - 1);
            j["note"] = std::string(noteBuf);
            if (bestDist > 2) {
                j["string"] = "";
                j["target"] = 0;
                j["inTune"] = false;
            } else {
                const float dev = static_cast<float>(note - kTargets[tuning_][best]) * 100.0f
                                  + t.cents();
                j["string"] = kStrings[best];
                j["target"] = kTargets[tuning_][best];
                j["inTune"] = std::fabs(dev) <= 5.0f;
            }
        }
        return j;
    }

    json perfJson() const
    {
        return json{{"cpu", 0.0}, {"remaining", 100}, {"xrun", 0}, {"tier", "Full"}};
    }

    json midiJson() const
    {
        json j = json::object();
        j["binds"] = json::array();
        for (const auto& b : host_->midiBindings()) {
            std::string target;
            if (b.kind == midi::MidiRouter::BindInfo::Kind::Param) {
                target = b.moduleId + "." + b.paramId;
            } else {
                target = "场景 " + std::to_string(b.sceneIndex);
            }
            j["binds"].push_back({{"cc", b.cc}, {"target", target}});
        }
        if (!learning_.active) {
            j["learning"] = nullptr;
        } else if (learning_.kind == "param") {
            j["learning"] = {{"kind", "param"}, {"module", learning_.module},
                             {"param", learning_.param}};
        } else {
            j["learning"] = {{"kind", "scene"}, {"index", learning_.sceneIndex}};
        }
        return j;
    }

    json abJson() const
    {
        json j = json::object();
        if (abA_.empty()) {
            j["a"] = nullptr;
        } else {
            j["a"] = true;
        }
        if (abB_.empty()) {
            j["b"] = nullptr;
        } else {
            j["b"] = true;
        }
        return j;
    }

    json undoJson() const
    {
        return json{{"canUndo", undoStack_.size() > 1}, {"canRedo", !redoStack_.empty()}};
    }

    json libraryJson() const
    {
        json j = json::object();
        j["models"] = json::array();
        for (const auto& m : models_) {
            j["models"].push_back({{"name", m.name}, {"type", m.type},
                                   {"brand", m.brand}, {"file", baseName(m.file)}});
        }
        j["irs"] = json::array();
        for (const auto& f : irs_) {
            j["irs"].push_back({{"name", stripExtension(baseName(f))}, {"file", baseName(f)}});
        }
        return j;
    }

    void pushUndo()
    {
        undoStack_.push_back(host_->exportPresetJson(current_.name.empty() ? "preset" : current_.name));
        if (undoStack_.size() > 50) {
            undoStack_.pop_front();
        }
        redoStack_.clear();
    }

    std::string wwwDir_;
    std::string demoDir_;
    std::string userDir_;
    std::string userPresetDir_;
    std::string userModelDir_;
    std::string userIrDir_;

    EngineHost* host_ = nullptr;
    std::unique_ptr<EngineHost> ownedHost_;
    AudioBackend* audioBackend_ = nullptr;
    mutable std::mutex shellMutex_;

    std::vector<PresetEntry> presets_[2]; // 0 = demo, 1 = user
    PresetEntry current_;
    std::vector<int> chainLayout_ = std::vector<int>(12, -1);
    std::vector<ModelInfo> models_;
    std::vector<std::string> irs_;
    mutable std::vector<std::string> modelPaths_;
    mutable std::vector<std::string> irPaths_;

    json output_ = json::object();
    bool tunerOn_ = true;
    int tuning_ = 0;
    bool dirty_ = false;
    std::string msg_ = "就绪";
    bool msgErr_ = false;

    std::deque<std::string> undoStack_;
    std::deque<std::string> redoStack_;
    std::string abA_;
    std::string abB_;

    struct Learning {
        bool active = false;
        std::string kind; // param | scene
        std::string module;
        std::string param;
        int sceneIndex = 0;
    };
    Learning learning_;

    std::mutex evMu_;
    std::vector<std::shared_ptr<SSEClient>> clients_;
    std::atomic<bool> running_{true};
    std::thread ticker_;
    std::thread pump_;
};

// ---------- static file serving ----------

namespace {

std::string urlDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = std::isdigit(static_cast<unsigned char>(s[i + 1]))
                               ? s[i + 1] - '0'
                               : (std::tolower(static_cast<unsigned char>(s[i + 1])) - 'a' + 10);
            const int lo = std::isdigit(static_cast<unsigned char>(s[i + 2]))
                               ? s[i + 2] - '0'
                               : (std::tolower(static_cast<unsigned char>(s[i + 2])) - 'a' + 10);
            if (hi >= 0 && hi < 16 && lo >= 0 && lo < 16) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string contentTypeFor(const std::string& path)
{
    const std::string ext = [&]() {
        const std::size_t dot = path.find_last_of('.');
        return dot == std::string::npos ? "" : lower(path.substr(dot));
    }();
    if (ext == ".html") {
        return "text/html; charset=utf-8";
    }
    if (ext == ".css") {
        return "text/css; charset=utf-8";
    }
    if (ext == ".js") {
        return "application/javascript; charset=utf-8";
    }
    if (ext == ".json") {
        return "application/json; charset=utf-8";
    }
    if (ext == ".woff2") {
        return "font/woff2";
    }
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".svg") {
        return "image/svg+xml";
    }
    if (ext == ".wav") {
        return "audio/wav";
    }
    if (ext == ".nam") {
        return "application/octet-stream";
    }
    return "application/octet-stream";
}

#ifdef _WIN32
// MIDI input (WinMM): completes MIDI learning with a hardware CC
void CALLBACK midiInputProc(HMIDIIN, UINT msg, DWORD_PTR inst, DWORD_PTR data1, DWORD_PTR)
{
    auto* hptr = reinterpret_cast<WebHost**>(inst);
    if (msg == MIM_DATA && hptr != nullptr && *hptr != nullptr) {
        const int status = static_cast<int>(data1 & 0xFF);
        if ((status & 0xF0) == 0xB0) {
            (*hptr)->onMidiCc(static_cast<int>((data1 >> 8) & 0x7F));
        }
    }
}
#endif

} // namespace

// ---------- HTTP handler (shared by standalone host and the shell) ----------

HttpHandler makeHandler(WebHost& host)
{
    return [&host](const HttpRequest& req, HttpResponse& resp, const StreamWriter& stream) {
        const std::string path = urlDecode(req.path);
        if (path == "/api/state" && req.method == "GET") {
            std::lock_guard<std::mutex> lock(host.shellMutex());
            resp.status = 200;
            resp.contentType = "application/json; charset=utf-8";
            json j = host.snapshot(false);
            j["catalog"] = nullptr; // 前端 catalog.js 为单源
            resp.body = j.dump();
            return;
        }
        if (path == "/api/cmd" && req.method == "POST") {
            resp.contentType = "application/json; charset=utf-8";
            json out;
            try {
                const json in = json::parse(req.body);
                std::lock_guard<std::mutex> lock(host.shellMutex());
                out = host.dispatch(in);
                host.broadcast({{"state", host.snapshot(false)}}, false);
            } catch (const std::exception& e) {
                out = json{{"ok", false}, {"error", std::string("JSON 解析失败: ") + e.what()}};
            }
            resp.body = out.dump();
            return;
        }
        if (path == "/api/events" && req.method == "GET") {
            // SSE 已在 HttpServer 中接管连接；这里注册并阻塞
            const auto client = host.addClient(stream);
            while (client->alive) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            host.removeClient(client);
            return;
        }
        if (path == "/api/import" && req.method == "PUT") {
            resp.contentType = "application/json; charset=utf-8";
            const std::string name = urlDecode(req.query);
            std::string clean = name;
            const std::size_t eq = clean.find("name=");
            clean = eq == std::string::npos ? clean : clean.substr(eq + 5);
            json out;
            if (req.body.size() > kMaxBody) {
                out = json{{"ok", false}, {"error", "文件过大"}};
            } else {
                std::lock_guard<std::mutex> lock(host.shellMutex());
                out = host.importFile(clean, req.body);
                host.broadcast({{"state", host.snapshot(false)}}, false);
            }
            resp.body = out.dump();
            return;
        }
        // static files (GET / HEAD)
        if (req.method == "GET" || req.method == "HEAD") {
            std::string rel = path;
            if (rel.empty() || rel == "/") {
                rel = "/index.html";
            }
            if (rel.find("..") != std::string::npos) {
                resp.status = 400;
                resp.body = "bad path";
                return;
            }
            const std::string full = joinPath(host.wwwDir(), rel.substr(1));
            std::string data;
            if (!readFile(full, data)) {
                resp.status = 404;
                resp.body = "not found";
                return;
            }
            resp.status = 200;
            resp.contentType = contentTypeFor(full);
            resp.body = std::move(data);
            return;
        }
        resp.status = 404;
        resp.body = "not found";
    };
}

// shell helpers (the WebView2 shell only holds a forward-declared WebHost)
WebHost* webHostCreate(EngineHost* host, const char* wwwDir, const char* demoDir,
                       AudioBackend* audioBackend)
{
    return new WebHost(host, std::string(wwwDir), std::string(demoDir), audioBackend);
}

void webHostStartTicker(WebHost& host)
{
    host.startTicker();
}

void webHostStopTicker(WebHost& host)
{
    host.stopTicker();
}

void webHostStopClients(WebHost& host)
{
    host.stopClients();
}

void webHostDestroy(WebHost* host)
{
    host->stopClients();
    delete host;
}

#ifndef NAMFX_WEB_EMBEDDED

// ---------- standalone main ----------

int run(int argc, char** argv)
{
    std::string wwwDir = NAMFX_WEBUI_DIR;
    std::string demoDir = NAMFX_DEMO_DIR;
    std::string bindAddr = "0.0.0.0";
    int port = kPort;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&](const char* name) -> std::string {
            return i + 1 < argc ? std::string(argv[++i]) : std::string(name);
        };
        if (a == "--port") {
            port = std::atoi(next("8810").c_str());
        } else if (a == "--bind") {
            bindAddr = next("0.0.0.0");
        } else if (a == "--www") {
            wwwDir = next(NAMFX_WEBUI_DIR);
        } else if (a == "--demo-dir") {
            demoDir = next(NAMFX_DEMO_DIR);
        } else if (a == "--help" || a == "-h") {
            std::printf("NAMFX WebUI host\n"
                        "  --port N        listen port (default 8810)\n"
                        "  --bind IP       bind address (default 0.0.0.0)\n"
                        "  --www DIR       web root (default %s)\n"
                        "  --demo-dir DIR  demo preset dir (default %s)\n",
                        NAMFX_WEBUI_DIR, NAMFX_DEMO_DIR);
            return 0;
        }
    }

    EngineHost engine;
    engine.prepare(48000.0, 128);
    desktop::NativeAudioBackend audio(engine);
    std::string audioError;
    audio.initialize(audioError);
    WebHost host(&engine, std::move(wwwDir), std::move(demoDir), &audio);

    HttpServer server(makeHandler(host));

    if (!server.start(bindAddr, port)) {
        std::fprintf(stderr, "cannot listen on %s:%d\n", bindAddr.c_str(), port);
        return 1;
    }
    std::printf("NAMFX WebUI host: http://%s:%d  (www=%s, demo=%s)\n",
                bindAddr.c_str(), port, host.wwwDir().c_str(), host.demoDir().c_str());

#ifdef _WIN32
    // MIDI input (WinMM): completes MIDI learning with a hardware CC
    std::thread midiThread([&host] {
        const UINT n = midiInGetNumDevs();
        if (n == 0) {
            return;
        }
        HMIDIIN h = nullptr;
        WebHost* hp = &host;
        for (UINT i = 0; i < n; ++i) {
            if (midiInOpen(&h, i, reinterpret_cast<DWORD_PTR>(&midiInputProc),
                           reinterpret_cast<DWORD_PTR>(&hp), CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
                midiInStart(h);
                std::this_thread::sleep_for(std::chrono::hours(24));
                midiInClose(h);
                return;
            }
        }
    });
    midiThread.detach();
#endif

    if (!host.hasAudioBackend()) {
        host.startAudioPump();
    }
    host.startTicker();
    std::printf("press Ctrl+C to stop\n");
    // control-plane host: run forever; Ctrl+C terminates the process
    for (;;) {
        std::this_thread::sleep_for(std::chrono::hours(1));
    }
}

#endif // NAMFX_WEB_EMBEDDED

} // namespace web
} // namespace namfx

#ifndef NAMFX_WEB_EMBEDDED

int main(int argc, char** argv)
{
    return namfx::web::run(argc, argv);
}

#endif // NAMFX_WEB_EMBEDDED
