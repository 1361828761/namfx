#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace namfx {
namespace preset {

class BackupManager {
public:
    explicit BackupManager(std::filesystem::path backupsDir, int maxVersions = 5);

    bool save(const std::string& presetName, const std::string& json);
    bool restore(const std::string& presetName, int version, std::string& outJson);
    std::vector<int> versions(const std::string& presetName) const;

private:
    std::filesystem::path dirFor(const std::string& presetName) const;

    std::filesystem::path backupsDir_;
    int maxVersions_ = 5;
};

} // namespace preset
} // namespace namfx
