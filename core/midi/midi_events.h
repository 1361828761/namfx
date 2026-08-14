#pragma once

#include <cstdint>

namespace namfx {
namespace midi {

// Engine-side MIDI event model (PLAN G4): plain POD, produced by the
// platform MIDI front-end (desktop USB-MIDI, embedded USB-MIDI later) and
// consumed by MidiRouter on the control thread.
struct Event {
    enum class Type : std::uint8_t {
        NoteOn,
        NoteOff,
        ControlChange,
        ProgramChange,
        PitchBend,
    };

    Type type;
    std::uint8_t channel = 0; // 0-15
    std::uint8_t data1 = 0;   // note / CC number / program number
    std::uint8_t data2 = 0;   // velocity / CC value (MSB)
    std::uint8_t data3 = 0;   // CC LSB (14-bit pairs), 0 otherwise
};

} // namespace midi
} // namespace namfx
