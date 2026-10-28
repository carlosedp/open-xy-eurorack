// Stratus: Clouds' granular mode, in fixed point, as an OP-XY send effect.
// The send is Clouds' input; the return is its output, all wet, while the
// stock mixer keeps each track's dry sound. Clouds itself lives in the send's
// buffer (core.h).
#include "core.h"
#include "clouds.h"
#include <cstring>

namespace stratus {
// Bytes of the buffer cleared per block before the first sound: 16 KiB, about
// 21 blocks (30 ms) for all of it.
const u32 kClearing = 16384;

// Clouds at rest, in a cleared buffer: GranularProcessor::Init and Prepare.
inline void reset(Clouds& k) {
    k.seed = 1;
    k.gain = kOne28;
    k.size_hint = 361267;  // Clouds' 1024 samples at 32 kHz, Q8 samples at 44.1 kHz
    k.lfo_value[0] = k.lfo_value[1] = kMaximum;  // stmlib's CosineOscillator starts at its peak
}

void initialize(State& state) { state.cleared = 0; }

// The send's 64 frames in place, at Clouds' eight knobs: two of its 32-sample
// blocks. Silent outside a send and while the buffer clears.
void render(State& state, opxy::Buffer buffer, opxy::Channel left, opxy::Channel right, const i32* knobs) {
    if (!buffer.words || buffer.count < sizeof(Clouds) / 4) {
        state.cleared = 0;
        for (u32 i = 0; i < opxy::block_frames; ++i) left[i] = right[i] = 0;
        return;
    }
    Clouds& clouds = *reinterpret_cast<Clouds*>(buffer.words);
    if (state.cleared < sizeof(Clouds)) {
        const u32 size = sizeof(Clouds) - state.cleared < kClearing ? sizeof(Clouds) - state.cleared : kClearing;
        std::memset(reinterpret_cast<unsigned char*>(&clouds) + state.cleared, 0, size);
        state.cleared += size;
        if (state.cleared == sizeof(Clouds)) reset(clouds);
        for (u32 i = 0; i < opxy::block_frames; ++i) left[i] = right[i] = 0;
        return;
    }
    const Controls controls = controls_of(knobs);
    for (u32 b = 0; b < opxy::block_frames; b += kBlock) {
        // The send in Clouds' units: full scale is its 1.0, Q26.
        i32 l[kBlock], r[kBlock];
        for (u32 t = 0; t < kBlock; ++t) {
            l[t] = shift_down(left[b + t], 5);
            r[t] = shift_down(right[b + t], 5);
        }
        process_block(clouds, controls, l, r);
        // Clouds' 16-bit output at full scale.
        for (u32 t = 0; t < kBlock; ++t) {
            left[b + t] = l[t] >= kUnit ? kMaximum : l[t] * 32;
            right[b + t] = r[t] >= kUnit ? kMaximum : r[t] * 32;
        }
    }
}

// The return against the OP-XY's dry as Clouds' wet against its own dry.
// Clouds plays its dry at 0.707 of the input and its wet at 0.707 x 1.2 of the
// grains, then halves both; the stock mixer plays the dry as it is. Clouds'
// halved, 0.707 dry is 1 / (2 sqrt 2) of the OP-XY's, so the return would rise
// by 2 sqrt 2 (9 dB); played against a full mix it rises by 4 (12 dB),
// saturating at full scale.
inline i32 balance(i32 x) {
    return x >= kMaximum / 4 ? kMaximum : x <= -kMaximum / 4 ? -kMaximum : x * 4;
}

// Clouds' knobs from the controls. Feedback rests at 0: its encoder press is
// the stock mixer's Dry.
void process(State& state, opxy::Buffer buffer, opxy::Channel left, opxy::Channel right,
             opxy::Parameters parameters) {
    i32 knobs[kKnobs];
    knobs[kPosition] = parameters[kControlPosition];
    knobs[kSize] = parameters[kControlSize];
    knobs[kPitch] = parameters[kControlPitch];
    knobs[kDensity] = parameters[kControlDensity];
    knobs[kFreeze] = parameters[kControlFreeze];
    knobs[kTexture] = parameters[kControlTexture];
    knobs[kFeedback] = 0;
    knobs[kReverbAmount] = parameters[kControlReverb];
    render(state, buffer, left, right, knobs);
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        left[i] = balance(left[i]);
        right[i] = balance(right[i]);
    }
}
}  // namespace stratus
