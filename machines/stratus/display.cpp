#include "display.h"
#include "clouds.h"

// Stratus' send page: the SDK's two-page header over Clouds' recording, a
// second of the send with now at its right end. A marker shows where
// Position starts the grains, and the grains appear there as Density
// schedules them, live as long as Size makes them, and drift as their Pitch
// reads the recording against the record head: toward now when pitched up,
// away when down, and forward through a frozen recording, which lights up.
// It is an impression from the controls: display code never sees the audio.
namespace stratus {
namespace draw = opxy::draw;

const u32 kStrip = 8, kMarker = 9, kFirstGrain = 10;
const i32 kLeft = 40, kRight = 440, kTop = 100, kBottom = 150;  // the recording, pixels
const i32 kWidth = kRight - kLeft;                               // a second of it
enum Shade : u32 { kDim, kMid, kBright };

// A control as the stock engines show one: its travel as 0..99, rounded, so
// that n reads back exactly from n / 99.
inline void caption(char* out, i32 value) {
    const u32 n = ((static_cast<u32>(control(value)) >> 16) * 99 + 16383) / 32767;
    if (n >= 10) *out++ = static_cast<char>('0' + n / 10);
    *out++ = static_cast<char>('0' + n % 10);
    *out = 0;
}

inline u32 next_random(u32& seed) {
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.initialized = 0;
}

inline void display(DisplayState& state, const draw::Input& input, Frame& frame) {
    const i32 unit = draw::unit;
    const bool frozen = input.choices[0] != 0;

    // The header: Position, Size, Pitch and Density, then Feedback, Texture,
    // Dry and Reverb, all 0..99. While Shift is held the edited row shows the
    // shift row instead: Freeze.
    const char* titles[8] = {"Position", "Size", "Pitch", "Density", "Feedback", "Texture", "Dry", "Reverb"};
    char values[8][draw::header_capacity] = {};
    for (u32 i = 0; i < 8; ++i) caption(values[i], input.slots[i]);
    if (input.shift) {
        u32 page = 0;
        for (u32 col = 0; col < opxy::page_controls; ++col)
            if (input.shown[col] >= opxy::page_controls) page = 1;
        for (u32 col = 0; col < opxy::page_controls; ++col) {
            const u32 slot = page * opxy::page_controls + col;
            titles[slot] = col ? "" : "Freeze";
            draw::copy(values[slot], draw::header_capacity, col ? "" : frozen ? "on" : "off");
        }
    }
    draw::page_header(state.header, input, frame, titles, values, 0);

    if (!state.initialized || state.track != input.track) {
        for (u32 i = 0; i < kShownGrains; ++i) state.grains[i].life = 0;
        state.seed = 1;
        state.wait = 0;
    }
    state.initialized = 1;
    state.track = input.track;

    // Clouds' controls from the page's words, as process reads them.
    i32 knobs[kKnobs];
    knobs[kPosition] = input.slots[kControlPosition];
    knobs[kSize] = input.slots[kControlSize];
    knobs[kPitch] = input.slots[kControlPitch];
    knobs[kDensity] = input.slots[kControlDensity];
    knobs[kFeedback] = input.slots[kControlFeedback];
    knobs[kTexture] = input.slots[kControlTexture];
    knobs[kReverbAmount] = input.slots[kControlReverb];
    knobs[kFreeze] = frozen ? kMaximum : 0;
    const Controls c = controls_of(knobs);
    // A grain's life: Clouds' size in samples at 44.1 kHz, as Q10 seconds.
    const i32 life = draw::clamp(mul31(lookup(kGrainSize, c.size), 194783), 8, 1024);  // samples / 256 / 44100 x 1024
    // Its pitch moves it through the recording at the ratio, a second a second;
    // the record head moves on at one, unless frozen.
    const i32 ratio = static_cast<i32>(exp2(octaves(c.pitch), 16));  // Q16
    const i32 drift = ((frozen ? ratio : ratio - 65536) * kWidth) / 64;  // Q10 pixels a second
    // Clouds plays up to about one grain more for each 1/23 of overlap.
    const i32 grains = c.overlap > 0 ? 1 + mul31(c.overlap, 23) : 0;
    const i32 dt = input.reduced_motion ? 0 : draw::clamp(input.elapsed, 0, 102);

    state.wait -= dt;
    if (!grains) state.wait = 0;
    for (u32 spawned = 0; grains && state.wait <= 0 && spawned < kShownGrains; ++spawned) {
        const i32 interval = life / grains > 0 ? life / grains : 1;
        // At regular intervals below Density's middle, at random above it.
        state.wait += c.deterministic ? interval
                                      : interval / 2 + static_cast<i32>(next_random(state.seed) >> 16) * interval / 65536;
        for (u32 i = 0; i < kShownGrains; ++i) {
            ShownGrain& g = state.grains[i];
            if (g.life) continue;
            g.x = (kRight - mul31(c.position, kWidth)) * unit;
            g.y = (kTop + 8 + static_cast<i32>(next_random(state.seed) >> 16) * (kBottom - kTop - 16) / 65536) * unit;
            g.age = 0;
            g.life = life;
            g.drift = drift;
            break;
        }
    }

    for (u32 i = 0; i < kShownGrains; ++i) {
        ShownGrain& g = state.grains[i];
        if (g.life) {
            g.age += dt;
            g.x += draw::floor_q10(g.drift * dt);
            // A frozen recording loops; otherwise a grain leaves at either end.
            if (frozen && g.x > kRight * unit) g.x -= kWidth * unit;
            if (g.age >= g.life || g.x < kLeft * unit || g.x > kRight * unit) g.life = 0;
        }
        if (!g.life) {
            draw::hide(frame, kFirstGrain + i);
            continue;
        }
        // It swells and fades over its life, from a quarter of its 4-pixel
        // radius to all of it: the artwork's circle is the largest.
        const i32 rest = g.life - g.age, nearest = g.age < rest ? g.age : rest;
        const i32 radius = unit / 4 + 3 * unit / 2 * nearest / g.life;
        draw::place(frame, kFirstGrain + i, g.x, g.y, radius, kBright, draw::fill_and_stroke);
    }
    draw::place(frame, kStrip, 0, 0, unit, frozen ? kBright : kDim, draw::stroke);
    draw::place(frame, kMarker, (kRight - mul31(c.position, kWidth)) * unit, (kTop - 4) * unit, unit, kMid,
                draw::fill_and_stroke);
}
}  // namespace stratus
