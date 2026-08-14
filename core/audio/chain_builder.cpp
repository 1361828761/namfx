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
        out.push_back(std::move(def));
    }
    return out;
}

} // namespace audio
} // namespace namfx
