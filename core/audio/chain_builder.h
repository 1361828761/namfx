#pragma once

#include "audio/slot.h"

#include <vector>

namespace namfx {
namespace audio {

class Chain;

// Chain editing (M5c): snapshot the live chain as a list of slot
// definitions with the CURRENT parameter values (ramped), so an edit
// (add / remove / reorder) can rebuild a fresh chain and swap it in.
// Control thread only.
std::vector<SlotDef> snapshotChain(const Chain& chain);

} // namespace audio
} // namespace namfx
