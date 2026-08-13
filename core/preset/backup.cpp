#include "preset/backup.h"

#include "preset/atomic_write.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace namfx {
namespace preset {

BackupManager::BackupManager(std::filesystem::path backupsDir, int maxVersions)
    : backupsDir_(std::move(backupsDir))
    , maxVersions_(std::max(maxVersions, 1))
{
}

std::filesystem::path BackupManager::dirFor(const std::string& presetName) const
{
    return backupsDir_ / presetName;
}

std::vector<int> BackupManager::versions(const std::string& presetName) const
{
    std::vector<int> result;
    const std::filesystem::path dir = dirFor(presetName);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return result;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::string stem = entry.path().stem().string();
        if (stem.size() < 2 || stem.front() != 'v') {
            continue;
        }
        try {
            result.push_back(std::stoi(stem.substr(1)));
        } catch (const std::exception&) {
            continue;
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool BackupManager::save(const std::string& presetName, const std::string& json)
{
    const std::filesystem::path dir = dirFor(presetName);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }
    std::vector<int> existing = versions(presetName);
    int next = existing.empty() ? 1 : existing.back() + 1;
    if (!writeAtomically(dir / ("v" + std::to_string(next) + ".json"), json)) {
        return false;
    }
    std::vector<int> all = versions(presetName);
    while (static_cast<int>(all.size()) > maxVersions_) {
        const int oldest = all.front();
        std::error_code ignored;
        std::filesystem::remove(dir / ("v" + std::to_string(oldest) + ".json"), ignored);
        all.erase(all.begin());
    }
    return true;
}

bool BackupManager::restore(const std::string& presetName, int version, std::string& outJson)
{
    const std::filesystem::path path = dirFor(presetName) / ("v" + std::to_string(version) + ".json");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string text = buffer.str();
    try {
        const nlohmann::json parsed = nlohmann::json::parse(text);
        (void)parsed;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    outJson = text;
    return true;
}

} // namespace preset
} // namespace namfx
