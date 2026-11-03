#include "display.h"
#include "clouds.h"

// Stratus' send page: the SDK's two-page header over Clouds' recording, a
// second of the send as a tape with now at its right end. The page cannot hear
// the send, so the tape's bars stand for it: written at the record head and
// moving left as the recording does, and still, lit, while it is frozen. A
// notch marks where Position starts the grains, and they appear there as
// Density schedules them, glow and swell with their window as Texture shapes
// it, light the tape they read and trail along it, drifting as their Pitch
// reads against the record head. Feedback writes their light back into the
// tape, and Reverb gives them a halo that lingers after them. It is an
// impression from the controls: display code never sees the audio.
namespace stratus {
namespace draw = opxy::draw;

// Nodes, in drawing order after the header.
const u32 kTape = 8, kFirstBar = 9, kRecord = kFirstBar + kBars, kMarker = kRecord + 1;
const u32 kFirstHalo = kMarker + 1, kFirstTrail = kFirstHalo + kShownGrains;
const u32 kFirstGrain = kFirstTrail + kShownGrains * kTrails;
static_assert(kFirstGrain + kShownGrains == kNodes, "Stratus' nodes");
const i32 kLeft = 40, kRight = 440, kMiddle = 130, kHalfHeight = 75;  // the tape, pixels
const i32 kWidth = kRight - kLeft;                                     // a second of it
const i32 kColumn = kWidth * draw::unit / static_cast<i32>(kBars);      // a bar's width, Q10 pixels
const i32 kShortestShown = 80;  // Q10 seconds: about five frames
// The palette, darkest to brightest (tools/clouds_panels.py).
enum Shade : u32 { kFaint, kDim, kLow, kMid, kHigh, kBright, kWhite };

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
        for (u32 i = 0; i < kShownGrains; ++i) state.grains[i].life = state.grains[i].glow = 0;
        for (u32 b = 0; b < kBars; ++b) {
            state.height[b] = 0;
            state.light[b] = 0;
        }
        state.head = 0;
        state.scroll = 0;
        state.walk = 128;
        state.phase = 0;
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
    // A grain shorter than a few frames would be drawn at whatever point of its
    // window a frame lands on, big and bright at some sizes and faint at
    // others: on screen it lives at least kShortestShown, its window spread
    // over that, and its peak size follows its real length.
    const i32 shown = life > kShortestShown ? life : kShortestShown;
    const i32 peak = unit * 2 / 5 + unit * 3 / 5 * (life < 512 ? life : 512) / 512;  // its largest scale
    // Its window, as Clouds shapes it (start_grain), for its glow.
    const bool smooth = c.window_shape >= 1 << 30;
    const i32 smoothness = smooth ? (c.window_shape - (1 << 30)) >> 15 : 0;
    const i32 slope = smooth ? 0 : static_cast<i32>(reciprocal(static_cast<u32>(c.window_shape + 21474836), 40));
    // Reverb's halo lingers up to two seconds after a grain, and spreads with
    // the diffuser (Texture above 75).
    const i32 linger = mul31(c.reverb, 2 * 1024);  // Q10 seconds
    const i32 spread = unit / 2 + mul31(c.reverb, unit / 2) + mul31(c.diffusion, unit / 2);  // the halo's scale

    // The grains alive now, for Feedback to write into the tape.
    u32 alive = 0;
    for (u32 i = 0; i < kShownGrains; ++i) alive += state.grains[i].life != 0;

    // The tape moves in from the record head, a second across, unless frozen.
    // Each new bar's height wanders, as a sound's level does, and Feedback
    // writes the grains back into it, taller and brighter.
    if (!frozen) {
        state.scroll += kWidth * dt;  // Q10 pixels
        while (state.scroll >= kColumn) {
            state.scroll -= kColumn;
            state.head = (state.head + 1) % kBars;
            state.walk += static_cast<i32>(next_random(state.seed) >> 24) - 128;
            state.walk = draw::clamp(state.walk, 48, 200);
            const i32 fed = mul31(c.feedback, static_cast<i32>(alive) * 24);  // Q8
            state.height[state.head] = static_cast<opxy::u8>(draw::clamp(state.walk + fed, 0, 255));
            state.light[state.head] = static_cast<opxy::u8>(draw::clamp(fed * 2, 0, 255));
        }
    }
    state.phase = (state.phase + dt) & 1023;

    state.wait -= dt;
    if (!grains) state.wait = 0;
    for (u32 spawned = 0; grains && state.wait <= 0 && spawned < kShownGrains; ++spawned) {
        const i32 interval = shown / grains > 0 ? shown / grains : 1;
        // At regular intervals below Density's middle, at random above it.
        state.wait += c.deterministic ? interval
                                      : interval / 2 + static_cast<i32>(next_random(state.seed) >> 16) * interval / 65536;
        for (u32 i = 0; i < kShownGrains; ++i) {
            ShownGrain& g = state.grains[i];
            if (g.life || g.glow) continue;
            g.x = g.origin = (kRight - mul31(c.position, kWidth)) * unit;
            g.y = (kMiddle - kHalfHeight + 12) * unit +
                  static_cast<i32>(next_random(state.seed) >> 16) * (2 * kHalfHeight - 24) / 65536 * unit;
            g.age = 0;
            g.life = shown;
            g.drift = drift;
            g.glow = 0;
            break;
        }
    }

    // The bars a grain lights as it reads them, this frame, Q15.
    i32 lit[kBars] = {};
    for (u32 i = 0; i < kShownGrains; ++i) {
        ShownGrain& g = state.grains[i];
        const u32 halo = kFirstHalo + i, grain = kFirstGrain + i;
        if (g.life) {
            g.age += dt;
            g.x += draw::floor_q10(g.drift * dt);
            // A frozen recording loops; otherwise a grain leaves at either end.
            if (frozen && g.x > kRight * unit) g.x -= kWidth * unit;
            if (g.age >= g.life || g.x < kLeft * unit || g.x > kRight * unit) {
                g.life = 0;
                g.glow = linger;
            }
        } else if (g.glow) {
            g.glow = g.glow > dt ? g.glow - dt : 0;
        }
        if (!g.life) {
            for (u32 k = 0; k < kTrails; ++k) draw::hide(frame, kFirstTrail + i * kTrails + k);
            draw::hide(frame, grain);
            // Reverb's halo fades where the grain was.
            if (g.glow && linger) {
                const i32 fade = g.glow * 32767 / linger;
                draw::place(frame, halo, g.x, g.y, spread * fade / 32768 + unit / 8, fade > 16384 ? kDim : kFaint,
                            draw::fill_and_stroke);
            } else {
                draw::hide(frame, halo);
            }
            continue;
        }
        // Its window's gain now, as Clouds renders it: its glow and its size.
        const u32 envelope = static_cast<u32>(g.age) * (0x80000000u / static_cast<u32>(g.life));
        const i32 gain = smooth ? gain_at<true, true>(envelope, smoothness, slope)
                                : gain_at<true, false>(envelope, smoothness, slope);  // Q15
        draw::place(frame, grain, g.x, g.y, unit / 5 + (peak - unit / 5) * gain / 32768, kMid + static_cast<u32>(3 * gain / 32768),
                    draw::fill_and_stroke);
        // Its trail: dots back along what it has read, dimmer and smaller.
        for (u32 k = 0; k < kTrails; ++k) {
            const i32 behind = g.x - (g.x - g.origin) / 3 * static_cast<i32>(k + 1);
            draw::place(frame, kFirstTrail + i * kTrails + k, behind, g.y,
                        unit / 4 * static_cast<i32>(kTrails - k) / static_cast<i32>(kTrails) + unit / 8, k ? kDim : kLow,
                        draw::fill_and_stroke);
        }
        // Reverb's halo, while it plays.
        if (linger) draw::place(frame, halo, g.x, g.y, spread * gain / 32768 + unit / 8, kFaint, draw::fill_and_stroke);
        else draw::hide(frame, halo);
        // The bar it reads lights up.
        const i32 back = (kRight * unit - state.scroll - g.x) / kColumn;
        if (back >= 0 && back < static_cast<i32>(kBars)) lit[back] = gain > lit[back] ? gain : lit[back];
    }

    // The tape's bars, newest at the record head; dim while recording, lit
    // while frozen, brighter where grains read them or Feedback wrote them.
    for (u32 k = 0; k < kBars; ++k) {
        const u32 b = (state.head + kBars - k) % kBars;
        const i32 x = kRight * unit - static_cast<i32>(k) * kColumn - state.scroll;
        if (x < kLeft * unit || !state.height[b]) {
            draw::hide(frame, kFirstBar + k);
            continue;
        }
        const i32 half = state.height[b] * kHalfHeight / 255 > 1 ? state.height[b] * kHalfHeight / 255 : 1;  // pixels
        const u32 written = frozen ? kLow : kDim + (state.light[b] > 128 ? 1u : 0u);
        const u32 shade = written + static_cast<u32>(2 * lit[k] / 32768);
        draw::place(frame, kFirstBar + k, x, kMiddle * unit, half * unit / kHalfHeight, shade > kHigh ? kHigh : shade,
                    draw::fill_and_stroke);
    }
    draw::place(frame, kTape, 0, 0, unit, frozen ? kMid : kLow, draw::stroke);
    // The record head: bright and blinking while it records, dim when frozen.
    draw::place(frame, kRecord, kRight * unit, kMiddle * unit, unit, frozen ? kDim : state.phase < 512 ? kWhite : kHigh,
                draw::fill_and_stroke);
    draw::place(frame, kMarker, (kRight - mul31(c.position, kWidth)) * unit, (kMiddle - kHalfHeight - 4) * unit, unit,
                kBright, draw::fill_and_stroke);
}
}  // namespace stratus
