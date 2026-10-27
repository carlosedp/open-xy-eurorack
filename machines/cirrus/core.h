#pragma once
#include <opxy/sample.hpp>
#include "fixed.h"

// Each OP-XY voice is one Clouds granular player, reading its grains from the
// note's sample where Clouds reads its recording: Clouds' 16-bit quality, with
// grains panned as Clouds spreads them. Clouds renders up to 32 grains at once;
// a voice renders up to kGrains, which caps the top of Density's range (see
// README.md).
namespace cirrus {
const u32 kBlock = 32;         // Clouds' kMaxBlockSize: the grain scheduler's block
// Density's scale: Clouds' grains in its stereo mode, for a stereo sample,
// and in its mono mode, for a mono one.
const u32 kMaxGrains = 32;
const u32 kMaxMonoGrains = 40;
const u32 kGrains = 6;         // grains a voice renders at once
// Clouds starts grains at high quality (Hermite) while fewer than a quarter of
// its grains are playing, then at medium quality: linear, with a plainer window.
const u32 kHighGrains = 2;
const u32 kMargin = 8;         // frames a grain may read past its span: Hermite's 4, and rounding
const u32 kFrames = 1u << 23;  // the longest sample read, 190 s at 44.1 kHz; frames past it are not
const u32 kShortest = 128;     // a region shorter than this plays nothing: room for the shortest grain at kMaxRatio

// The controls, in the order of definition.json: four, then four on Shift.
enum Slot : u32 { kPosition, kSize, kDensity, kTexture, kPitch, kSpeed, kSpread };

// Clouds' Grain, with its envelope rendered as it plays and its pan.
struct Grain {
    u32 active;
    u32 high;                // GRAIN_QUALITY_HIGH, else MEDIUM
    u32 first;               // its next frame in the sample, phase aside
    u32 phase;               // frames read past first, Q16, below one frame between blocks
    u32 increment;           // the pitch ratio, Q16
    u32 pre_delay;           // samples to wait within the block it starts in
    u32 envelope;            // the envelope's phase, 0..2 in Q30
    u32 envelope_increment;  // 2 / width, Q30
    i32 smoothness;          // the window's share of the envelope, Q15
    i32 slope;               // the trapezoid's slope, Q10
    i32 gain_l, gain_r;      // the pan, Q31
};

// The parameters one 32-sample block uses: Clouds' Parameters, already scaled.
struct Controls {
    i32 position;      // Q31, 0..1
    i32 size;          // Q31, 0..1
    i32 overlap;       // Q31: Density's distance from its middle
    u32 deterministic; // Density below its middle: grains at regular intervals
    i32 window_shape;  // Q31, 0..1
    i32 semitones;     // Pitch and the pad's tuning, Q16
    i32 speed;         // the scan, Q16 frames of the sample per output sample at its own rate
    i32 spread;        // Q31, 0..1
};

struct Voice {
    opxy::SampleView sample;
    u32 seed;               // stmlib Random's state
    u32 trigger;            // a note started: seed a grain at once
    u32 scan;               // where the grains start before Position, Q32 of the region, at the block's start
    u32 step;               // how far the scan moves in a block, Q32 of the region
    // Grains (GranularSamplePlayer).
    Grain grains[kGrains];
    i32 num_grains;         // the playing grains, smoothed, Q16
    i32 gain;               // the grains' gain normalization, smoothed, Q28
    i32 size_hint;          // recent grain sizes, smoothed, Q8 samples
    i32 phasor;             // samples since the last grain, Q8
};
}  // namespace cirrus
