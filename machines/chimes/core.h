#pragma once
#include <opxy/audio.hpp>
#include <opxy/lease_pool.hpp>
#include "fixed.h"

// Each OP-XY voice is one of Rings' four voices at polyphony 4, the setting
// Rings itself plays chords with: 12 modal partials, or a main string and one
// sympathetic string. Rings at polyphony 1 runs 60 partials and 8 strings, about
// eight times the work of an OP-XY voice.
namespace chimes {
// Rings' models that Chimes plays, in control order.
enum Model : u32 { kModal, kSympathetic, kString, kChord, kModels };

const unsigned kModes = 12;    // Rings: 64 / polyphony - 4
const unsigned kStrings = 2;   // Rings: 2 x 4 / polyphony
// A string's delay line: 2048 samples reach 21.5 Hz at 44.1 kHz, as in Rings.
const unsigned kLine = 2048, kLineMask = kLine - 1;
// The string model's dispersion all-pass borrows the second line's first half.
const unsigned kStretchMask = kLine / 2 - 1;
// The plucker's comb, as in Rings.
const unsigned kPluckMask = 255;

// The trapezoidal state-variable filter's two integrators (see modal.h).
struct Svf { i32 s1, s2; };

// One voice's lines, leased from a shared pool while a string model sounds.
// Q26 like the rest of the audio: a string at a high note loses less than a
// 16-bit step per pass, so 16-bit lines would hold it at a constant offset.
struct Lines {
    i32 strings[kStrings][kLine];
    i32 pluck[kPluckMask + 1];
};

struct String {
    u32 frequency;      // Q32, gliding toward its target
    u32 period;         // the loop's delay, Q16 samples, at the end of the last block
    u32 comb;           // the pickup's delay, Q16 samples, likewise
    i32 ap_in, ap_out;  // the fractional-delay all-pass's last input and output, Q26
    i32 x1, x2;         // the FIR's last two inputs, Q26
    Svf iir;            // the IIR damping low-pass
    DcBlocker coupling; // the dispersion's AC coupling
    i32 noise;          // filtered dispersion noise, Q26
    i32 bridge;         // the curved bridge's displacement, Q26
    u32 lfo;            // pickup modulation phase for a sympathetic string, Q32
};

struct Voice {
    u32 model;   // the model this state is set up for; kModels before the first block
    u32 strum;   // a note started: excite on the next block
    u32 lease;   // the Lines lease, 0 when none
    u32 write;   // the next write position in every line
    u32 seed;    // noise
    i32 peak;    // the limiter's envelope, Q26
    DcBlocker coupling;  // the output's AC coupling
    // Modal: each partial's integrators and coefficients.
    Svf modes[kModes];
    i32 rg[kModes], hg[kModes], g2[kModes];
    // How far Rings' float partial would have decayed since the strike ended:
    // octaves left before it falls below kLife (life), and per block (decay), Q24.
    i32 life[kModes], decay[kModes];
    u32 count;    // partials below 0.49 of the sample rate
    u32 key[4];   // the frequency and controls the coefficients were computed for
    u32 pending;  // blocks left retuning a quarter of the partials to key[0]
    u32 quarter;  // the quarter retuned last
    Svf exciter;  // the strike's low-pass
    // Strings and the plucker that excites them.
    String strings[kStrings];
    Svf pluck_filter;
    i32 pluck_rg, pluck_hg, pluck_g;  // its low-pass coefficients
    u32 pluck_remaining;              // noise samples left in the burst
    u32 pluck_ringing;                // blocks the comb still rings after the burst
    u32 pluck_period;                 // comb delay, Q16 samples
    i32 pluck_gain;                   // comb feedback, Q31
    DcBlocker blocker;                // the exciter's DC blocker
};
// Lines for 12 string voices at once; a 13th steals the oldest, which falls
// silent until struck again.
struct Shared {
    opxy::LeasePool<Lines, 12> lines;
};
}  // namespace chimes
