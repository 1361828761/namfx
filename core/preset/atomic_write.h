#pragma once

#include <filesystem>
#include <string>

namespace namfx {
namespace preset {

bool writeAtomically(const std::filesystem::path& target, const std::string& content);

} // namespace preset
} // namespace namfx
