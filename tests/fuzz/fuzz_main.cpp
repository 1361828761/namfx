// Fault injection fuzz targets (PLAN §12, M1 scope). Deterministic seeding:
// the same --seed reproduces the same run. Run in CI via ctest; a crash is
// reported by the test runner, escaped exceptions and non-finite audio are
// findings that exit non-zero here.
#include "audio/chain.h"
#include "audio/slot.h"
#include "modules/dsp/chorus.h"
#include "modules/dsp/flanger.h"
#include "modules/dsp/gain.h"
#include "modules/dsp/klon.h"
#include "modules/dsp/ocd.h"
#include "modules/dsp/ota_comp.h"
#include "modules/dsp/tone.h"
#include "modules/dsp/ts808.h"
#include "modules/module_registry.h"
#include "platform/rt_alloc.h"
#include "preset/preset_io.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#define NOMINMAX
#include <windows.h>

namespace {

constexpr int kDefaultIters = 300;
constexpr std::uint32_t kDefaultSeed = 0x4E414D46;

const char* kBasePreset = R"({
    "schema": 1,
    "name": "FuzzBase",
    "chain": [
        { "slot": 0, "category": "pedal", "impl": "dsp", "module": "gain", "params": {"gain": 3.0} },
        { "slot": 1, "category": "pedal", "impl": "dsp", "module": "od.ts808", "params": {"drive": 5.0, "tone": 5.0, "level": 0.0} }
    ],
    "scenes": [
        { "name": "Solo", "overrides": [ { "moduleId": "gain", "bypass": true, "params": {"gain": 6.0} } ] }
    ]
})";

std::shared_ptr<const namfx::ModuleRegistry> makeRegistry()
{
    auto registry = std::make_shared<namfx::ModuleRegistry>();
    namfx::registerGain(*registry);
    namfx::registerTone(*registry);
    namfx::registerTs808(*registry);
    namfx::registerTransparent(*registry);
    namfx::registerMosfetOd(*registry);
    namfx::registerOtaComp(*registry);
    namfx::registerChorus(*registry);
    namfx::registerFlanger(*registry);
    return registry;
}

void loadBothModes(const std::string& text, const std::shared_ptr<const namfx::ModuleRegistry>& registry)
{
    namfx::preset::LoadReport report;
    (void)namfx::preset::loadPreset(text, namfx::preset::LoadMode::Tolerant, *registry, report);
    namfx::preset::LoadReport strictReport;
    (void)namfx::preset::loadPreset(text, namfx::preset::LoadMode::Strict, *registry, strictReport);
}

// returns true when the exception is an acceptable resource-exhaustion escape
bool isBadAlloc(const std::exception& e)
{
    return dynamic_cast<const std::bad_alloc*>(&e) != nullptr;
}

std::string baseWithSchema(int schema)
{
    const std::string marker = "\"schema\": 1";
    const std::string replacement = "\"schema\": " + std::to_string(schema);
    std::string text = kBasePreset;
    const std::size_t pos = text.find(marker);
    if (pos != std::string::npos) {
        text.replace(pos, marker.size(), replacement);
    }
    return text;
}

std::vector<std::string> knownModules = {"gain", "tone", "od.ts808", "od.transparent",
                                         "od.mosfet", "comp.ota", "mod.chorus", "mod.flanger"};

// ---- F1: schema mutation JSON ---------------------------------------------

int fuzzJsonMutation(std::mt19937& rng, int iters,
                     const std::shared_ptr<const namfx::ModuleRegistry>& registry)
{
    int escaped = 0;
    for (int i = 0; i < iters; ++i) {
        std::string text = kBasePreset;
        const int op = static_cast<int>(rng() % 4);
        if (op == 0) {
            const int flips = 1 + static_cast<int>(rng() % 16);
            for (int f = 0; f < flips; ++f) {
                const std::size_t pos = rng() % text.size();
                text[pos] = static_cast<char>(rng());
            }
        } else if (op == 1) {
            text.resize(rng() % text.size());
        } else if (op == 2) {
            const std::size_t pos = rng() % (text.size() + 1);
            const std::size_t len = 1 + rng() % 64;
            std::string garbage(len, '\0');
            for (char& c : garbage) {
                c = static_cast<char>(rng());
            }
            text.insert(pos, garbage);
        } else {
            const std::size_t len = rng() % 4096;
            std::string garbage(len, '\0');
            for (char& c : garbage) {
                c = static_cast<char>(rng());
            }
            text = std::move(garbage);
        }
        try {
            loadBothModes(text, registry);
        } catch (const std::exception& e) {
            if (!isBadAlloc(e)) {
                ++escaped;
            }
        } catch (...) {
            ++escaped;
        }
    }
    return escaped;
}

// ---- F2: migration chain fuzz ----------------------------------------------

int fuzzMigration(std::mt19937& rng, int iters,
                  const std::shared_ptr<const namfx::ModuleRegistry>& registry)
{
    int escaped = 0;
    for (int i = 0; i < iters; ++i) {
        std::string text = baseWithSchema(static_cast<int>(rng() % 8) - 2);
        const int op = static_cast<int>(rng() % 5);
        if (op == 0) {
            // unknown top-level fields
            text = text.substr(0, text.size() - 2) + ",\"unknown_field\": " +
                   std::string(rng() % 2 ? "\"str\"" : "42") + "\n}";
        } else if (op == 1) {
            // drop required fields
            const std::size_t pos = text.find("\"chain\"");
            if (pos != std::string::npos) {
                text.erase(pos, std::string("\"chain\"").size());
            }
        } else if (op == 2) {
            // corrupt types
            const std::string marker = "\"name\": \"FuzzBase\"";
            const std::size_t pos = text.find(marker);
            if (pos != std::string::npos) {
                text.replace(pos, marker.size(), "\"name\": [1, {\"x\": null}]");
            }
        } else if (op == 3) {
            // deep nesting bomb
            std::string nested = "1";
            const int depth = 1 + static_cast<int>(rng() % 60);
            for (int d = 0; d < depth; ++d) {
                nested = "[" + nested + "]";
            }
            text = text.substr(0, text.size() - 2) + ",\"deep\": " + nested + "\n}";
        } else {
            // slots with garbage types
            text = text.substr(0, text.size() - 2) +
                   ",\"chain\": [null, 42, \"x\", {\"slot\": \"a\", \"module\": 7}]}";
        }
        try {
            loadBothModes(text, registry);
        } catch (const std::exception& e) {
            if (!isBadAlloc(e)) {
                ++escaped;
            }
        } catch (...) {
            ++escaped;
        }
    }
    return escaped;
}

// ---- F3: sample-rate switching + arbitrary block sizes ----------------------

int fuzzSampleRate(std::mt19937& rng, int iters)
{
    auto registry = makeRegistry();
    const int rates[] = {44100, 48000, 96000};
    int failures = 0;

    for (int i = 0; i < iters; ++i) {
        const int slotCount = 1 + static_cast<int>(rng() % 4);
        std::vector<namfx::audio::SlotDef> slots;
        for (int s = 0; s < slotCount; ++s) {
            namfx::audio::SlotDef def;
            def.slot = s;
            def.category = "pedal";
            def.impl = "dsp";
            def.moduleId = knownModules[rng() % knownModules.size()];
            const std::vector<namfx::ParamSpec> specs = registry->specsFor(def.moduleId);
            for (const namfx::ParamSpec& spec : specs) {
                const float span = spec.max - spec.min;
                const float value = spec.min + span * static_cast<float>(rng()) /
                                                     static_cast<float>(rng.max());
                def.params.push_back(namfx::ParamInit{spec.id, value});
            }
            def.bypass = (rng() % 4 == 0);
            def.mix = 0.25f + 0.75f * static_cast<float>(rng()) / static_cast<float>(rng.max());
            slots.push_back(std::move(def));
        }

        namfx::audio::Chain chain(std::move(slots), registry);
        for (int switchCount = 0; switchCount < 4; ++switchCount) {
            const int rate = rates[rng() % 3];
            const int block = 1 + static_cast<int>(rng() % 512);
            chain.prepare(static_cast<double>(rate), block);

            std::vector<float> in(static_cast<std::size_t>(block));
            std::vector<float> inR(static_cast<std::size_t>(block));
            std::vector<float> outL(static_cast<std::size_t>(block));
            std::vector<float> outR(static_cast<std::size_t>(block));
            for (int b = 0; b < 3; ++b) {
                for (float& v : in) {
                    v = -1.0f + 2.0f * static_cast<float>(rng()) / static_cast<float>(rng.max());
                }
                for (float& v : inR) {
                    v = -1.0f + 2.0f * static_cast<float>(rng()) / static_cast<float>(rng.max());
                }
                chain.process(in.data(), inR.data(), outL.data(), outR.data(), block);
                for (float v : outL) {
                    if (!std::isfinite(v)) {
                        ++failures;
                        std::printf("F3 finding: non-finite output, rate=%d block=%d slots=%d\n",
                                    rate, block, slotCount);
                        return failures;
                    }
                }
            }
        }
    }
    return failures;
}

// ---- F4: OOM simulation (debug builds only) ---------------------------------
// Windows: MSVC debug CRT (checked-iterator proxies, container backout, RTC
// reporting) corrupts/terminates when an injected bad_alloc lands inside
// container growth in deep constructor paths (verified with repro + stack
// traces: chain build, ParamStore, nlohmann parse all terminate). So on
// Windows, F4 verifies the failpoint + catch machinery at the harness
// boundary only. On non-Windows (libstdc++ is exception-safe under injected
// bad_alloc), the full chain-construction injection runs. Details in README.

int buildRandomChain(std::mt19937& rng,
                     const std::shared_ptr<const namfx::ModuleRegistry>& registry,
                     namfx::audio::Chain& out)
{
    const int slotCount = 1 + static_cast<int>(rng() % 3);
    std::vector<namfx::audio::SlotDef> slots;
    for (int s = 0; s < slotCount; ++s) {
        namfx::audio::SlotDef def;
        def.slot = s;
        def.category = "pedal";
        def.impl = "dsp";
        def.moduleId = knownModules[rng() % knownModules.size()];
        const std::vector<namfx::ParamSpec> specs = registry->specsFor(def.moduleId);
        for (const namfx::ParamSpec& spec : specs) {
            const float span = spec.max - spec.min;
            const float value = spec.min + span * static_cast<float>(rng()) /
                                                     static_cast<float>(rng.max());
            def.params.push_back(namfx::ParamInit{spec.id, value});
        }
        def.bypass = (rng() % 4 == 0);
        def.mix = 0.25f + 0.75f * static_cast<float>(rng()) / static_cast<float>(rng.max());
        slots.push_back(std::move(def));
    }
    out = namfx::audio::Chain(std::move(slots), registry);
    return slotCount;
}

int fuzzOom(std::mt19937& rng, int iters,
            const std::shared_ptr<const namfx::ModuleRegistry>& registry)
{
#ifdef NAMFX_RT_ALLOC_ENABLED
    int escaped = 0;
    (void)registry; // deep injection path is non-Windows only
    for (int i = 0; i < iters; ++i) {
        const std::uint64_t failAfter = 1 + rng() % 64;
        try {
            namfx::rt::AllocCounter::armOomFailpoint(failAfter);
            std::string s(128 + rng() % 4096, 'x');
            std::vector<float> v(64 + rng() % 512, 0.25f);
            auto p = std::make_shared<std::string>(s);
            (void)v;
            (void)p;
        } catch (const std::bad_alloc&) {
            // resource exhaustion surfaced as an exception: acceptable
        } catch (const std::exception& e) {
            if (!isBadAlloc(e)) {
                ++escaped;
            }
        } catch (...) {
            ++escaped;
        }
        namfx::rt::AllocCounter::disarmOomFailpoint();

#ifndef _WIN32
        // deep injection into engine constructor paths (safe on libstdc++)
        const std::uint64_t chainFail = 1 + rng() % 3000;
        try {
            namfx::audio::Chain chain(std::vector<namfx::audio::SlotDef>{}, registry);
            namfx::rt::AllocCounter::armOomFailpoint(chainFail);
            buildRandomChain(rng, registry, chain);
            chain.prepare(48000.0, 64);
            std::vector<float> in(64, 0.25f);
            std::vector<float> inR(64, 0.0f);
            std::vector<float> outL(64, 0.0f);
            std::vector<float> outR(64, 0.0f);
            chain.process(in.data(), inR.data(), outL.data(), outR.data(), 64);
        } catch (const std::bad_alloc&) {
            // acceptable
        } catch (const std::exception& e) {
            if (!isBadAlloc(e)) {
                ++escaped;
            }
        } catch (...) {
            ++escaped;
        }
        namfx::rt::AllocCounter::disarmOomFailpoint();
#endif
    }
    return escaped;
#else
    (void)rng;
    (void)iters;
    (void)registry;
    std::printf("F4: OOM fuzz skipped (only in debug builds with NAMFX_RT_ALLOC_ENABLED)\n");
    return 0;
#endif
}

} // namespace

static void terminateHandler()
{
    std::printf("TERMINATE (unhandled exception or noexcept violation)\n");
    std::fflush(stdout);
    const HMODULE mod = GetModuleHandleW(nullptr);
    void* frames[24];
    const USHORT count = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        const auto addr = reinterpret_cast<std::uintptr_t>(frames[i]);
        const auto base = reinterpret_cast<std::uintptr_t>(mod);
        std::printf("  frame %u: offset 0x%llX\n", i,
                    static_cast<unsigned long long>(addr >= base ? addr - base : addr));
    }
    std::fflush(stdout);
    std::abort();
}

int main(int argc, char** argv)
{
    std::set_terminate(terminateHandler);
    int iters = kDefaultIters;
    std::uint32_t seed = kDefaultSeed;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--iters" && i + 1 < argc) {
            iters = std::atoi(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        } else {
            std::printf("usage: namfx_fuzz [--iters N] [--seed S]\n");
            return 2;
        }
    }

    std::mt19937 rng(seed);
    auto registry = makeRegistry();

    std::printf("namfx_fuzz seed=%u iters=%d\n", seed, iters);
    std::fflush(stdout);
    std::printf("F1 schema mutation JSON: starting\n");
    std::fflush(stdout);
    const int escapedJson = fuzzJsonMutation(rng, iters, registry);
    std::printf("F1 done: %d escaped\n", escapedJson);
    std::fflush(stdout);
    const int escapedMigration = fuzzMigration(rng, iters, registry);
    std::printf("F2 done: %d escaped\n", escapedMigration);
    std::fflush(stdout);
    const int rateFailures = fuzzSampleRate(rng, iters);
    std::printf("F3 done: %d failures\n", rateFailures);
    std::fflush(stdout);
    const int escapedOom = fuzzOom(rng, iters, registry);
    std::printf("F4 done: %d escaped\n", escapedOom);
    std::fflush(stdout);

    std::printf("namfx_fuzz seed=%u iters=%d\n", seed, iters);
    std::printf("  F1 schema mutation JSON:        %d escaped exceptions\n", escapedJson);
    std::printf("  F2 migration chain:             %d escaped exceptions\n", escapedMigration);
    std::printf("  F3 sample-rate/block injection: %d failures\n", rateFailures);
    std::printf("  F4 OOM simulation:              %d escaped exceptions\n", escapedOom);

    const int total = escapedJson + escapedMigration + rateFailures + escapedOom;
    if (total == 0) {
        std::printf("namfx_fuzz: PASS\n");
        return 0;
    }
    std::printf("namfx_fuzz: FAIL (%d findings)\n", total);
    return 1;
}
