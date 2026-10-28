#pragma once
#include <opxy/effect.hpp>
#include "fixed.h"

// Stratus is one Clouds in granular mode on a send, in its 16-bit stereo
// setting: the send records into Clouds' buffer and grains play it back, then
// Clouds' diffuser, feedback and reverb, with Clouds' 32 grains at once.
// Everything but the clearing count lives in the send's buffer (Clouds, below).
namespace stratus {
// Clouds' recording in its 16-bit stereo mode, 32696 samples at 32 kHz
// (1.02 s), at 44.1 kHz.
const i32 kBuffer = 45056;
const i32 kTail = 8;           // Clouds' kInterpolationTail: the first samples, repeated after the end
const u32 kCrossFade = 256;    // Clouds' kCrossFadeSize: the splice when recording resumes
const u32 kBlock = 32;         // Clouds' kMaxBlockSize: its processing block
const u32 kMaxGrains = 32;     // Clouds' grains in its stereo mode: Density's scale
const u32 kGrains = 32;        // grains rendered at once
// Clouds starts grains at high quality while fewer than a quarter of its 32
// are playing, then at medium quality: linear reading and a plainer window.
const u32 kHighGrains = 8;
const u32 kDiffusion = 2784;   // the diffuser's eight lines (clouds.h)
const u32 kReverb = 22589;     // the reverb's ten lines (clouds.h)

// The four controls, then the four on Shift (definition.json).
// Clouds' eight knobs, as the DSP reads them (controls_of).
enum Knob : u32 { kPosition, kSize, kDensity, kTexture, kPitch, kFeedback, kReverbAmount, kFreeze, kKnobs };
// The controls definition.json declares: Clouds' four main ones, then on a
// press of the same encoders Freeze, Texture, Dry and Reverb. Dry is the stock
// mixer's (machine.json dryParameter): Stratus never reads it.
enum Control : u32 {
    kControlPosition, kControlSize, kControlPitch, kControlDensity,
    kControlFreeze, kControlTexture, kControlDry, kControlReverb
};

// Clouds' Grain, its envelope rendered in steps as it plays.
struct Grain {
    u32 active;
    u32 high;                // GRAIN_QUALITY_HIGH, else MEDIUM
    i32 first;               // its first sample in the recording
    u32 phase;               // samples read, Q16
    u32 increment;           // the pitch ratio, Q16
    u32 pre_delay;           // samples to wait within the block it starts in
    u32 envelope;            // the envelope's phase, 0..2 in Q30
    u32 envelope_increment;  // 2 / width, Q30
    i32 smoothness;          // the window's share of the envelope, Q15
    i32 slope;               // the trapezoid's slope, Q10
};

// stmlib's Svf: the trapezoidal state-variable filter's two integrators, Q26.
struct Svf { i32 s1, s2; };

// The parameters one 32-sample block uses: Clouds' Parameters, already scaled.
struct Controls {
    i32 position;      // Q31, 0..1
    i32 size;          // Q31, 0..1
    i32 overlap;       // Q31: Density's distance from its middle
    u32 deterministic; // Density below its middle: grains at regular intervals
    i32 window_shape;  // Q31, 0..1
    i32 diffusion;     // Q31, 0..1
    i32 pitch;         // semitones, Q16
    i32 feedback;      // Q31, 0..1
    i32 reverb;        // Q31, 0..1
    u32 freeze;
};

// Clouds in the send's buffer, which starts as whatever was there: process
// clears it in slices before the first sound.
struct Clouds {
    // Recording (AudioBuffer), a channel each, Q26 within Clouds' 16-bit range:
    // at 32 bits, as a send well below full scale keeps its detail.
    i32 left[kBuffer + kTail];
    i32 right[kBuffer + kTail];
    i32 splice[2][kCrossFade + 2];  // Clouds' tail_buffer_, recorded while frozen
    i32 head;                       // the next sample written
    u32 crossfade;                  // Clouds' crossfade_counter_
    // Grains (GranularSamplePlayer).
    Grain grains[kGrains];
    u32 seed;                       // stmlib Random's state
    i32 num_grains;                 // the playing grains, smoothed, Q16
    i32 gain;                       // the grains' gain normalization, smoothed, Q28
    i32 size_hint;                  // recent grain sizes, smoothed, Q8 samples
    i32 phasor;                     // samples since the last grain, Q8
    // Feedback: the last block's output, high-passed.
    i32 feedback[2][kBlock];
    Svf highpass[2];
    i32 freeze_lp;                  // Freeze, smoothed: it hands feedback over to the reverb, Q31
    // Diffuser and reverb (FxEngine): each delay line a ring, with its position.
    i32 diffusion[kDiffusion];
    u32 diffusion_position[8];
    i32 reverb[kReverb];
    u32 reverb_position[10];
    u32 lfo[2];                     // the reverb's two modulations, Q32 phase
    i32 lfo_value[2];               // 0..1 as stmlib's CosineOscillator, Q31
    i32 damping[2];                 // the reverb loop's low-passes, Q26
};
// definition.json resources.bufferBytes.
const u32 kBufferBytes = 465748;
static_assert(sizeof(Clouds) <= kBufferBytes && kBufferBytes % 4 == 0, "Clouds fits the declared send buffer");

// At most opxy::effect_buffered_state_bytes; storage starts zeroed and is never destroyed.
struct State {
    u32 cleared;  // bytes of Clouds cleared since initialize
};
void initialize(State& state);
void process(State& state, opxy::Buffer buffer, opxy::Channel left, opxy::Channel right,
             opxy::Parameters parameters);
// process at all of Clouds' knobs, kKnobs Q31 words in Knob order.
void render(State& state, opxy::Buffer buffer, opxy::Channel left, opxy::Channel right, const i32* knobs);
}  // namespace stratus
