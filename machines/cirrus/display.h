#pragma once
#include <opxy/page_header.hpp>
#include <opxy/sample_overview.hpp>

namespace cirrus {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Paths 0 to 3 are the sample's waveform; nodes 4 to 11 and text slots 0 to
// 15 the SDK's two-page header; node 12 the scan, 13 to 28 the grains, 29 and
// 30 the trim's start and end; text slot 16 asks for a sample.
const unsigned kShownGrains = 16;
using Frame = opxy::draw::Frame<4, 50, 15 + kShownGrains, 17, 3>;

// A grain as the screen shows it: an impression from the controls and the
// notes, since the screen runs apart from the audio and never sees the voices.
struct ShownGrain {
    opxy::u32 frame;  // where it reads in the region, Q8 frames
    opxy::i32 y;      // Q10 pixels
    opxy::i32 age;    // Q10 seconds; the grain is gone at life
    opxy::i32 life;   // Q10 seconds, 0 when the slot is free
};
struct DisplayState {
    opxy::draw::PageHeader header;
    ShownGrain grains[kShownGrains];
    opxy::u32 seed;
    opxy::u32 track;
    opxy::u32 initialized;
    opxy::u32 serial;      // the last note start seen
    opxy::u32 generation;  // the waveform's overview, 0 for none drawn
    opxy::u32 key;         // the waveform outlines' geometry key
    opxy::i32 scan;        // how far the scan has moved since the last note, Q8 frames
    opxy::i32 wait;        // Q10 seconds until the next grain
};
}  // namespace cirrus
