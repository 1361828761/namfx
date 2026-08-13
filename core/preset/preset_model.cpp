#include "preset/preset_model.h"

namespace namfx {
namespace preset {

bool operator==(const SceneOverride& a, const SceneOverride& b)
{
    if (a.moduleId != b.moduleId || a.bypass != b.bypass || a.params.size() != b.params.size()) {
        return false;
    }
    for (const ParamInit& pa : a.params) {
        bool found = false;
        for (const ParamInit& pb : b.params) {
            if (pa.id == pb.id && pa.value == pb.value) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool operator==(const SceneDef& a, const SceneDef& b)
{
    if (a.name != b.name || a.overrides.size() != b.overrides.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.overrides.size(); ++i) {
        if (!(a.overrides[i] == b.overrides[i])) {
            return false;
        }
    }
    return true;
}

bool operator==(const Preset& a, const Preset& b)
{
    if (a.schema != b.schema || a.name != b.name || a.chain.size() != b.chain.size()
        || a.scenes.size() != b.scenes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.chain.size(); ++i) {
        const audio::SlotDef& sa = a.chain[i];
        const audio::SlotDef& sb = b.chain[i];
        if (sa.slot != sb.slot || sa.category != sb.category || sa.impl != sb.impl
            || sa.moduleId != sb.moduleId || sa.bypass != sb.bypass || sa.mix != sb.mix) {
            return false;
        }
        if (sa.params.size() != sb.params.size()) {
            return false;
        }
        for (const ParamInit& pa : sa.params) {
            bool found = false;
            for (const ParamInit& pb : sb.params) {
                if (pa.id == pb.id && pa.value == pb.value) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
    }
    for (std::size_t i = 0; i < a.scenes.size(); ++i) {
        if (!(a.scenes[i] == b.scenes[i])) {
            return false;
        }
    }
    return true;
}

bool operator!=(const Preset& a, const Preset& b)
{
    return !(a == b);
}

} // namespace preset
} // namespace namfx
