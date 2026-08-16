#pragma once

#include <string>
#include <vector>

namespace namfx {
namespace web {

struct AudioDeviceTypeInfo {
    std::string name;
    std::vector<std::string> devices;
};

struct AudioBackendState {
    std::vector<AudioDeviceTypeInfo> types;
    std::string type;
    std::string device;
    std::string error;
    double sampleRate = 0.0;
    int blockSize = 0;
    bool active = false;
};

class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    virtual bool initialize(std::string& error) = 0;
    virtual void shutdown() = 0;
    virtual AudioBackendState state() const = 0;
    virtual bool apply(const std::string& type, const std::string& device,
                       double sampleRate, int blockSize, std::string& error) = 0;
    // periodic control-plane heartbeat (host ticker): lets backends perform
    // time-based work (e.g. delayed un-mute after a device switch) without
    // their own timers/threads
    virtual void tick() {}
};

} // namespace web
} // namespace namfx
