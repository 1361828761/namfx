#include "preset/atomic_write.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace namfx {
namespace preset {

namespace {

bool writeFile(const std::filesystem::path& path, const std::string& content)
{
#ifdef _WIN32
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t written = std::fwrite(content.data(), 1, content.size(), file);
    const bool ok = written == content.size() && std::fflush(file) == 0
        && ::_commit(::_fileno(file)) == 0;
    std::fclose(file);
    return ok;
#else
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.flush();
        if (!stream) {
            return false;
        }
    }
    const int fd = ::open(path.string().c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    const int result = ::fsync(fd);
    ::close(fd);
    return result == 0;
#endif
}

void syncDirectory(const std::filesystem::path& dir)
{
#ifndef _WIN32
    const int fd = ::open(dir.string().c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#else
    (void)dir;
#endif
}

} // namespace

bool writeAtomically(const std::filesystem::path& target, const std::string& content)
{
    const std::filesystem::path dir = target.parent_path();
    std::error_code ec;
    if (!dir.empty() && !std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            return false;
        }
    }
    const std::filesystem::path temp = target.string() + ".tmp";
    if (!writeFile(temp, content)) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
    }
    if (std::rename(temp.string().c_str(), target.string().c_str()) != 0) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
    }
    syncDirectory(dir);
    std::ifstream verify(target, std::ios::binary);
    if (!verify) {
        return false;
    }
    std::ostringstream buffer;
    buffer << verify.rdbuf();
    return buffer.str() == content;
}

} // namespace preset
} // namespace namfx
