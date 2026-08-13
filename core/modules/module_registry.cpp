#include "modules/module_registry.h"

#include "modules/module_base.h"

#include <stdexcept>
#include <utility>

namespace namfx {

bool ModuleRegistry::registerModule(const std::string& id, std::string category,
                                    std::vector<ParamSpec> specs,
                                    std::function<std::unique_ptr<ModuleBase>()> factory)
{
    if (index_.find(id) != index_.end()) {
        return false;
    }
    Entry entry;
    entry.id = id;
    entry.category = std::move(category);
    entry.specs = std::move(specs);
    entry.factory = std::move(factory);
    index_.emplace(id, entries_.size());
    entries_.push_back(std::move(entry));
    return true;
}

std::unique_ptr<ModuleBase> ModuleRegistry::create(const std::string& id) const
{
    const auto it = index_.find(id);
    if (it == index_.end()) {
        throw std::out_of_range("unknown module id: " + id);
    }
    return entries_[it->second].factory();
}

const ParamSpec* ModuleRegistry::findParam(const std::string& moduleId, const std::string& paramId) const
{
    const auto it = index_.find(moduleId);
    if (it == index_.end()) {
        return nullptr;
    }
    for (const ParamSpec& spec : entries_[it->second].specs) {
        if (spec.id == paramId) {
            return &spec;
        }
    }
    return nullptr;
}

const std::vector<ParamSpec>& ModuleRegistry::specsFor(const std::string& moduleId) const
{
    const auto it = index_.find(moduleId);
    if (it == index_.end()) {
        throw std::out_of_range("unknown module id: " + moduleId);
    }
    return entries_[it->second].specs;
}

const std::string& ModuleRegistry::categoryOf(const std::string& moduleId) const
{
    const auto it = index_.find(moduleId);
    if (it == index_.end()) {
        throw std::out_of_range("unknown module id: " + moduleId);
    }
    return entries_[it->second].category;
}

std::vector<std::string> ModuleRegistry::allIds() const
{
    std::vector<std::string> ids;
    ids.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        ids.push_back(entry.id);
    }
    return ids;
}

} // namespace namfx
