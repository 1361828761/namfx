#include "audio/param_store.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace namfx {

namespace {

int rampSamples(double sampleRate, double ms)
{
    const double total = sampleRate * ms / 1000.0;
    if (total < 1.0) {
        return 1;
    }
    return static_cast<int>(total);
}

} // namespace

ParamStore::ParamStore(std::vector<ParamSpec> specs)
{
    slots_.reserve(specs.size());
    for (ParamSpec& spec : specs) {
        Slot slot;
        slot.value = spec.defaultValue;
        slot.target = spec.defaultValue;
        index_.emplace(spec.id, slots_.size());
        slot.spec = std::move(spec);
        slots_.push_back(std::move(slot));
    }
}

void ParamStore::setSampleRate(double sampleRate)
{
    sampleRate_ = sampleRate;
    for (Slot& slot : slots_) {
        startRamp(slot);
    }
}

void ParamStore::set(const std::string& id, float value)
{
    Slot& slot = slotFor(id);
    slot.target = clampTo(slot.spec, value);
    startRamp(slot);
}

void ParamStore::setImmediate(const std::string& id, float value)
{
    Slot& slot = slotFor(id);
    const float clamped = clampTo(slot.spec, value);
    slot.value = clamped;
    slot.target = clamped;
    slot.remaining = 0;
    slot.step = 0.0f;
}

float ParamStore::get(const std::string& id) const
{
    return slotFor(id).value;
}

float ParamStore::target(const std::string& id) const
{
    return slotFor(id).target;
}

std::size_t ParamStore::indexOf(const std::string& id) const
{
    const auto it = index_.find(id);
    if (it == index_.end()) {
        throw std::out_of_range("unknown param id: " + id);
    }
    return it->second;
}

float ParamStore::getByIndex(std::size_t index) const
{
    return slots_.at(index).value;
}

void ParamStore::setByIndex(std::size_t index, float value)
{
    Slot& slot = slots_.at(index);
    slot.target = clampTo(slot.spec, value);
    startRamp(slot);
}

void ParamStore::advance(int n)
{
    if (n <= 0) {
        return;
    }
    for (Slot& slot : slots_) {
        if (slot.remaining <= 0) {
            continue;
        }
        const int steps = std::min(n, slot.remaining);
        slot.value += slot.step * static_cast<float>(steps);
        slot.remaining -= steps;
        if (slot.remaining == 0) {
            slot.value = slot.target;
        }
    }
}

bool ParamStore::isRamping() const
{
    for (const Slot& slot : slots_) {
        if (slot.remaining > 0) {
            return true;
        }
    }
    return false;
}

const ParamStore::Slot& ParamStore::slotFor(const std::string& id) const
{
    const auto it = index_.find(id);
    if (it == index_.end()) {
        throw std::out_of_range("unknown param id: " + id);
    }
    return slots_[it->second];
}

ParamStore::Slot& ParamStore::slotFor(const std::string& id)
{
    const auto it = index_.find(id);
    if (it == index_.end()) {
        throw std::out_of_range("unknown param id: " + id);
    }
    return slots_[it->second];
}

void ParamStore::startRamp(Slot& slot)
{
    if (slot.value == slot.target) {
        slot.remaining = 0;
        slot.step = 0.0f;
        return;
    }
    const int total = rampSamples(sampleRate_, kMaxSmoothingMs);
    slot.remaining = total;
    slot.step = (slot.target - slot.value) / static_cast<float>(total);
}

float ParamStore::clampTo(const ParamSpec& spec, float value)
{
    return std::min(std::max(value, spec.min), spec.max);
}

} // namespace namfx
