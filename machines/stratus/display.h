#pragma once
#include <opxy/page_header.hpp>

namespace stratus {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Nodes 0 to 7 and every text slot are the SDK's two-page header; then, in
// drawing order, the recording's tape, its bars, the record head, Position's
// marker, the grains' halos, their trails and the grains (display.cpp).
const unsigned kShownGrains = 24, kTrails = 2, kBars = 96;
const unsigned kNodes = 8 + 1 + kBars + 1 + 1 + kShownGrains * (2 + kTrails);
using Frame = opxy::draw::Frame<0, 0, kNodes, 16, 7>;

// A grain as the page shows it: an impression from the controls, since the
// page runs apart from the audio and never sees Clouds' own grains.
struct ShownGrain {
    opxy::i32 x, y;    // Q10 pixels
    opxy::i32 origin;  // where it started, Q10 pixels
    opxy::i32 age;     // Q10 seconds; the grain is gone at life
    opxy::i32 life;    // Q10 seconds, 0 when the slot is free
    opxy::i32 drift;   // Q10 pixels a second
    opxy::i32 glow;    // Q10 seconds its halo still lingers once it is gone
};
struct DisplayState {
    opxy::draw::PageHeader header;
    ShownGrain grains[kShownGrains];
    // The tape: a bar for each of its columns, the newest at the record head,
    // with its height and how bright it was written, Q8.
    opxy::u8 height[kBars], light[kBars];
    opxy::u32 head;     // the newest column
    opxy::i32 scroll;   // how far the newest column has moved in, Q10 pixels
    opxy::i32 walk;     // the written height, wandering, Q8
    opxy::i32 phase;    // the record head's blink, Q10 seconds
    opxy::u32 seed;
    opxy::u32 track;
    opxy::u32 initialized;
    opxy::i32 wait;     // Q10 seconds until the next grain
};
}  // namespace stratus
