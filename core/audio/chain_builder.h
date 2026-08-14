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

// Reorder: move the def with the given slot id so it ends up at position
// dstIndex (0-based, in the current list order). Sets ok=false when the
// slot is unknown or the index is out of range.
std::vector<SlotDef> reorderChain(const std::vector<SlotDef>& slots, int slot, int dstIndex,
                                  bool& ok);

} // namespace audio
} // namespace namfx
