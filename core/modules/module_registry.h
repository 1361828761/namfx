#pragma once

#include "modules/param_spec.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace namfx {

class ModuleBase;

class ModuleRegistry {
public:
    bool registerModule(const std::string& id, std::string category,
                        std::vector<ParamSpec> specs,
                        std::function<std::unique_ptr<ModuleBase>()> factory);

    bool has(const std::string& id) const
    {
        return index_.find(id) != index_.end();
    }

    std::unique_ptr<ModuleBase> create(const std::string& id) const;
    const ParamSpec* findParam(const std::string& moduleId, const std::string& paramId) const;
    const std::vector<ParamSpec>& specsFor(const std::string& moduleId) const;
    const std::string& categoryOf(const std::string& moduleId) const;
    std::vector<std::string> allIds() const;

private:
    struct Entry {
        std::string id;
        std::string category;
        std::vector<ParamSpec> specs;
        std::function<std::unique_ptr<ModuleBase>()> factory;
    };

    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::size_t> index_;
};

} // namespace namfx
