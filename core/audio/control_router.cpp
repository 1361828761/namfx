#include "audio/control_router.h"

#include <stdexcept>
#include <utility>

namespace namfx {
namespace audio {

namespace {

std::string makeKey(const std::string& moduleId, const std::string& paramId)
{
    std::string key = moduleId;
    key.push_back('/');
    key += paramId;
    return key;
}

} // namespace

bool ControlRouter::bind(const Chain& chain, int sourceId, const std::string& moduleId,
                         const std::string& paramId)
{
    if (sourceId < 0 || sourceId >= kMaxSources) {
        return false;
    }
    const int slot = chain.slotIndexOf(moduleId);
    if (slot < 0) {
        return false;
    }
    std::size_t paramIndex = 0;
    try {
        paramIndex = chain.paramIndexOf(slot, paramId);
    } catch (const std::out_of_range&) {
        return false;
    }

    const Table* oldTable = table_.load(std::memory_order_acquire);
    auto fresh = std::make_unique<Table>();
    if (oldTable) {
        fresh->bindings = oldTable->bindings;
        fresh->index = oldTable->index;
    }
    const std::string key = makeKey(moduleId, paramId);
    const auto it = fresh->index.find(key);
    if (it != fresh->index.end()) {
        Binding& b = fresh->bindings[it->second];
        if (b.sourceId != sourceId) {
            return false; // same param bound to a different source: reject
        }
        b.slot = slot;
        b.paramIndex = paramIndex;
        const Table* old = table_.exchange(fresh.release(), std::memory_order_acq_rel);
        (void)old;
        const Table* released = nullptr;
        while (releaseQueue_.pop(released)) {
            delete released;
        }
        return true;
    }
    if (fresh->bindings.size() >= static_cast<std::size_t>(kMaxBindings)) {
        return false;
    }
    Binding b;
    b.sourceId = sourceId;
    b.slot = slot;
    b.paramIndex = paramIndex;
    b.key = key;
    fresh->index.emplace(key, fresh->bindings.size());
    fresh->bindings.push_back(std::move(b));
    const Table* old = table_.exchange(fresh.release(), std::memory_order_acq_rel);
    (void)old;
    const Table* released = nullptr;
    while (releaseQueue_.pop(released)) {
        delete released;
    }
    return true;
}

void ControlRouter::unbind(const Chain& chain, const std::string& moduleId,
                           const std::string& paramId)
{
    const std::string key = makeKey(moduleId, paramId);
    const Table* oldTable = table_.load(std::memory_order_acquire);
    if (!oldTable || oldTable->index.find(key) == oldTable->index.end()) {
        return;
    }
    auto fresh = std::make_unique<Table>();
    for (const Binding& b : oldTable->bindings) {
        if (b.key == key) {
            continue;
        }
        fresh->index.emplace(b.key, fresh->bindings.size());
        fresh->bindings.push_back(b);
    }
    const Table* old = table_.exchange(fresh.release(), std::memory_order_acq_rel);
    (void)old;
    const Table* released = nullptr;
    while (releaseQueue_.pop(released)) {
        delete released;
    }
    (void)chain;
}

bool ControlRouter::isBound(const std::string& moduleId, const std::string& paramId) const
{
    const Table* table = table_.load(std::memory_order_acquire);
    return table && table->index.find(makeKey(moduleId, paramId)) != table->index.end();
}

int ControlRouter::boundCount() const
{
    const Table* table = table_.load(std::memory_order_acquire);
    return table ? static_cast<int>(table->bindings.size()) : 0;
}

void ControlRouter::setSourceValue(int sourceId, float value)
{
    if (sourceId < 0 || sourceId >= kMaxSources) {
        return;
    }
    sourceValues_[static_cast<std::size_t>(sourceId)].store(value, std::memory_order_relaxed);
    sourceHeartbeat_[static_cast<std::size_t>(sourceId)].fetch_add(1, std::memory_order_relaxed);
}

bool ControlRouter::uiSet(const std::string& moduleId, const std::string& paramId, float value)
{
    UiCommand cmd;
    cmd.key = makeKey(moduleId, paramId); // pre-built on the control thread
    cmd.moduleId = moduleId;
    cmd.paramId = paramId;
    cmd.value = value;
    return uiQueue_.push(cmd);
}

bool ControlRouter::uiSetBypass(const std::string& moduleId, bool bypass)
{
    UiCommand cmd;
    cmd.moduleId = moduleId;
    cmd.value = bypass ? 1.0f : 0.0f;
    cmd.isBypass = true;
    return uiQueue_.push(cmd);
}

void ControlRouter::apply(Chain& chain, int frames)
{
    tick_ += frames;
    const long hangFrames = static_cast<long>(kHangTimeMs * sampleRate_ / 1000.0);
    const Table* table = table_.load(std::memory_order_acquire);

    // UI commands first, independent of the binding table: bound params
    // queue the value (deep-1), unbound write straight to the store target;
    // no allocation on this thread
    while (uiQueue_.pop(uiCmdBuf_)) {
        if (uiCmdBuf_.isBypass) {
            // bypass flips fade state: audio-thread only, like scene recall
            const int slot = chain.slotIndexOf(uiCmdBuf_.moduleId);
            if (slot >= 0) {
                chain.setBypassByIndex(slot, uiCmdBuf_.value != 0.0f);
            }
            continue;
        }
        if (table != nullptr) {
            const auto it = table->index.find(uiCmdBuf_.key);
            if (it != table->index.end()) {
                const std::size_t idx = it->second;
                if (idx < table->bindings.size()) {
                    Pending& p = pending_[idx];
                    p.active = true;
                    p.value = uiCmdBuf_.value;
                    continue;
                }
            }
        }
        const int slot = chain.slotIndexOf(uiCmdBuf_.moduleId);
        if (slot >= 0) {
            try {
                chain.setParamByIndex(slot, chain.paramIndexOf(slot, uiCmdBuf_.paramId),
                                      uiCmdBuf_.value);
            } catch (const std::out_of_range&) {
                // unknown param: drop
            }
        }
    }
    if (table == nullptr) {
        return;
    }
    if (table != lastTable_) {
        // new binding table: retire the previous one and reset pending state
        if (lastTable_ != nullptr) {
            releaseQueue_.push(lastTable_);
        }
        lastTable_ = table;
        for (auto& p : pending_) {
            p = Pending{};
        }
    }
    const std::size_t bindingCount = table->bindings.size();

    for (std::size_t i = 0; i < bindingCount; ++i) {
        const Binding& b = table->bindings[i];
        const long heartbeat =
            sourceHeartbeat_[static_cast<std::size_t>(b.sourceId)].load(std::memory_order_relaxed);
        Pending& p = pending_[i];
        const bool slotValid = b.slot < chain.slotCount();
        if (!slotValid) {
            p.active = false;
            continue;
        }
        if (heartbeat != p.lastHeartbeat) {
            // source just spoke: mark active and follow its value
            p.lastHeartbeat = heartbeat;
            p.lastActiveTick = tick_;
            chain.setParamByIndex(b.slot, b.paramIndex,
                                  sourceValues_[static_cast<std::size_t>(b.sourceId)].load(
                                      std::memory_order_relaxed));
            continue;
        }
        const bool sourceActive = (tick_ - p.lastActiveTick) <= hangFrames;
        if (sourceActive) {
            chain.setParamByIndex(b.slot, b.paramIndex,
                                  sourceValues_[static_cast<std::size_t>(b.sourceId)].load(
                                      std::memory_order_relaxed));
            continue;
        }
        if (p.active) {
            // source released: ease back to the queued UI value
            chain.setParamByIndex(b.slot, b.paramIndex, p.value);
        }
        p.active = false;
    }
}

} // namespace audio
} // namespace namfx
