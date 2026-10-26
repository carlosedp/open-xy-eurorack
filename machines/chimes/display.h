#pragma once
#include <opxy/draw.hpp>

namespace chimes {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Twelve chimes hang from a rail, three to each of four outlines (paths 0 to 3).
using Frame = opxy::draw::Frame<4, 39, 4, 0, 3>;
const unsigned kTubes = 12;
struct DisplayState {
    opxy::i32 swing[kTubes];  // each chime's swing, tenths of a degree, Q10
    opxy::u32 phase[kTubes];  // its pendulum phase, Q32 cycles
    opxy::u32 serial;         // the last note start seen
    opxy::u32 track;
    opxy::u32 initialized;
    opxy::i32 structure;      // the Structure the outlines were drawn for
    opxy::u32 key;            // the outlines' geometry key
};
}
