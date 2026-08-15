#include "audio/chain_builder.h"

#include "audio/chain.h"

#include <stdexcept>
#include <utility>

namespace namfx {
namespace audio {

std::vector<SlotDef> snapshotChain(const Chain& chain)
{
    std::vector<SlotDef> out;
    const int n = chain.slotCount();
    for (int i = 0; i < n; ++i) {
        SlotDef def;
        try {
            def = chain.defOf(i);
        } catch (const std::out_of_range&) {
            continue;
        }
        // parameters carry their current (ramped) values, not the preset
        // defaults, so an edit never reverts what the user dialed in
        def.params.clear();
        const std::vector<ParamSpec>& specs = chain.specsOf(i);
        for (std::size_t p = 0; p < specs.size(); ++p) {
            def.params.push_back(ParamInit{specs[p].id, chain.paramValue(i, p)});
        }
        // the effective mix (UI override or preset value) is kept as well
        def.mix = chain.mixValueOf(i);
        out.push_back(std::move(def));
    }
    return out;
}

std::vector<SlotDef> reorderChain(const std::vector<SlotDef>& slots, int slot, int dstIndex,
                                  bool& ok)
{
    ok = false;
    std::vector<SlotDef> out = slots;
    std::size_t src = out.size();
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i].slot == slot) {
            src = i;
            break;
        }
    }
    if (src == out.size() || dstIndex < 0 || dstIndex > static_cast<int>(out.size())) {
        return out;
    }
    SlotDef def = std::move(out[src]);
    out.erase(out.begin() + static_cast<std::ptrdiff_t>(src));
    // dstIndex is the FINAL position (0..N, N = end of list); after the
    // removal the insert index shifts left when the target was past the
    // source
    int ins = dstIndex;
    if (dstIndex > src) {
        ins = dstIndex - 1;
    }
    ins = std::max(0, std::min(ins, static_cast<int>(out.size())));
    out.insert(out.begin() + ins, std::move(def));
    ok = true;
    return out;
}

} // namespace audio
} // namespace namfx
