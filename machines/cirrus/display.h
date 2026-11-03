#pragma once
#include <opxy/page_header.hpp>
#include <opxy/sample_overview.hpp>

namespace cirrus {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Paths 0 to 3 are the sample's waveform; nodes 4 to 11 and text slots 0 to
// 15 the SDK's two-page header; then, in drawing order, the lit waveform under
// the grains, where the scans start, the trim's ends, the held notes'
// playheads, the grains' trails and the grains (display.cpp); text slot 16
// asks for a sample.
const unsigned kShownGrains = 16, kPlayheads = 4, kTrails = 2;
const unsigned kNodes = 12 + kShownGrains + 1 + 2 + kPlayheads + kShownGrains * kTrails + kShownGrains;
using Frame = opxy::draw::Frame<4, 50, kNodes, 17, 7>;

// A grain as the screen shows it: an impression from the controls and the
// notes, since the screen runs apart from the audio and never sees the voices.
struct ShownGrain {
    opxy::u32 frame;   // where it reads in the region, Q8 frames
    opxy::u32 origin;  // where it started reading
    opxy::i32 y;       // Q10 pixels
    opxy::i32 age;     // Q10 seconds; the grain is gone at life
    opxy::i32 life;    // Q10 seconds, 0 when the slot is free
};
// A held note's scan, as the screen follows it.
struct Playhead {
    opxy::u32 active;
    opxy::u32 serial;  // when it started, to replace and release the oldest first
    opxy::i32 scan;    // how far it has moved since its note, Q8 frames
    opxy::i32 wait;    // Q10 seconds until its next grain
    opxy::u32 seeded;  // its note's own grain has started
    opxy::i32 age;     // Q10 seconds since its note, for the note's pulse
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
    opxy::u32 order;       // playheads started since the screen opened
    Playhead heads[kPlayheads];
};
}  // namespace cirrus
