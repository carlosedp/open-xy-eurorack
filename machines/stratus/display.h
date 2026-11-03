#pragma once
#include <opxy/page_header.hpp>

namespace stratus {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Nodes 0 to 7 and every text slot are the SDK's two-page header; node 8 is
// the recording, 9 Position's marker, and 10 on the grains.
const unsigned kShownGrains = 24;
using Frame = opxy::draw::Frame<0, 0, 10 + kShownGrains, 16, 3>;

// A grain as the page shows it: an impression from the controls, since the
// page runs apart from the audio and never sees Clouds' own grains.
struct ShownGrain {
    opxy::i32 x, y;  // Q10 pixels
    opxy::i32 age;   // Q10 seconds; the grain is gone at life
    opxy::i32 life;  // Q10 seconds, 0 when the slot is free
    opxy::i32 drift; // Q10 pixels a second
};
struct DisplayState {
    opxy::draw::PageHeader header;
    ShownGrain grains[kShownGrains];
    opxy::u32 seed;
    opxy::u32 track;
    opxy::u32 initialized;
    opxy::i32 wait;  // Q10 seconds until the next grain
};
}  // namespace stratus
