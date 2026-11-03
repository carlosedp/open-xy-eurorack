#include "display.h"
#include <opxy/sample_engine.hpp>
#include "granular.h"

// Cirrus' own screen over the stock sampler's page: the SDK's two-page header
// over the track's sample, its waveform from the sample's overview. While
// notes are held, a line shows where the scan starts the grains, Position
// into the trim and moving on at Speed from each note, and the grains appear
// there as Density schedules them, live as long as Size makes them, reading
// on at their Pitch. It is an impression from the controls and the notes:
// display code never sees the voices.
namespace cirrus {
namespace draw = opxy::draw;

const u32 kWave = 0, kFirstHeader = 4, kScan = 12, kFirstGrain = 13, kMessage = 16;
const i32 kLeft = 40, kWidth = 400, kMiddle = 125, kHalfHeight = 25;  // the waveform, pixels
const u32 kBinsPerPath = 24;                                           // and one shared with the next
enum Shade : u32 { kDim, kMid, kBright };

// A control as the stock engines show one: its travel as 0..99, rounded, so
// that n reads back exactly from n / 99.
inline void caption(char* out, i32 value) {
    const u32 n = ((static_cast<u32>(control(value)) >> 16) * 99 + 16383) / 32767;
    if (n >= 10) *out++ = static_cast<char>('0' + n / 10);
    *out++ = static_cast<char>('0' + n % 10);
    *out = 0;
}

// Where a frame of the sample sits across the waveform, Q10 pixels.
inline i32 x_of(u32 frame, u32 frames) {
    const unsigned t = top_bit(frames);
    const u32 fraction = multiply_shift(frame < frames ? frame : frames - 1, reciprocal(frames, 31 + t), t);  // Q31
    return (kLeft + mul31(static_cast<i32>(fraction), kWidth)) * draw::unit;
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.initialized = 0;
}

// Path p's centre across the screen, Q10 pixels: its points sit about it,
// within the 64 pixels a path may reach.
inline i32 centre_of(u32 p) {
    return kLeft * draw::unit + static_cast<i32>(p * kBinsPerPath + kBinsPerPath / 2) * kWidth * draw::unit /
                                    static_cast<i32>(draw::overview_bins - 1);
}

// The waveform: each path traces 25 bins along their highest values and back
// along their lowest, at least a pixel apart so that every outline has area.
inline void draw_wave(DisplayState& state, const draw::SampleOverview& o, Frame& frame) {
    for (u32 p = 0; p < Frame::path_count; ++p) {
        Frame::path_type& path = frame.paths[p];
        for (u32 i = 0; i <= kBinsPerPath; ++i) {
            const u32 bin = p * kBinsPerPath + i < draw::overview_bins ? p * kBinsPerPath + i : draw::overview_bins - 1;
            const i32 x = kLeft * draw::unit + static_cast<i32>(bin) * kWidth * draw::unit /
                                                   static_cast<i32>(draw::overview_bins - 1) - centre_of(p);
            i32 top = -o.high[bin] * kHalfHeight * draw::unit / 32768;
            i32 bottom = -o.low[bin] * kHalfHeight * draw::unit / 32768;
            if (bottom - top < draw::unit) {
                const i32 middle = (top + bottom) / 2;
                top = middle - draw::unit / 2;
                bottom = middle + draw::unit / 2;
            }
            path.points[i].x = x;
            path.points[i].y = top;
            path.points[2 * kBinsPerPath + 1 - i].x = x + 1;  // distinct from the top's
            path.points[2 * kBinsPerPath + 1 - i].y = bottom;
        }
    }
    state.key = state.key % 0x7ffffffeu + 1;
    for (u32 p = 0; p < Frame::path_count; ++p) frame.paths[p].key = state.key;
}

inline void display(DisplayState& state, const draw::Input& input, Frame& frame) {
    const i32 unit = draw::unit;

    // The header: Position, Size, Density and Texture, then Pitch, Speed and
    // Spread, with Texture again where an eighth control would be; all 0..99.
    const char* const titles[8] = {"Position", "Size", "Density", "Texture", "Pitch", "Speed", "Spread", ""};
    char values[8][draw::header_capacity] = {};
    for (u32 i = 0; i < 7; ++i) caption(values[i], input.slots[i]);
    const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 3}};
    draw::page_header(state.header, input, frame, titles, values, kFirstHeader, pages);

    if (!state.initialized || state.track != input.track) {
        for (u32 i = 0; i < kShownGrains; ++i) state.grains[i].life = 0;
        state.seed = 1;
        state.serial = input.note_serial;
        state.generation = 0;
        state.scan = 0;
        state.wait = 0;
    }
    state.initialized = 1;
    state.track = input.track;

    const draw::SampleOverview& o = *opxy_sample_overview();
    const bool ready = o.assigned && o.loaded && o.frames >= kShortest && o.end > o.begin;
    draw::text(frame, kMessage, 200 * unit, 128 * unit, unit, !o.assigned ? "load a sample" : !ready ? "loading" : "");
    if (!ready) {
        for (u32 p = 0; p < Frame::path_count; ++p) draw::hide(frame, kWave + p);
        draw::hide(frame, kScan);
        for (u32 i = 0; i < kShownGrains; ++i) draw::hide(frame, kFirstGrain + i);
        state.generation = 0;
        return;
    }
    if (o.generation != state.generation) {
        draw_wave(state, o, frame);
        state.generation = o.generation;
    }
    for (u32 p = 0; p < Frame::path_count; ++p) draw::place(frame, kWave + p, centre_of(p), kMiddle * unit, unit, kMid, draw::stroke);

    // The controls as the voices read them, and the trim the grains stay in.
    const Controls c = controls_of(input.slots);
    const u32 rate = o.rate >= 8000 && o.rate <= 192000 ? o.rate : 44100;
    const u32 region = o.end - o.begin < kFrames ? o.end - o.begin : kFrames - 1;  // as the voices read it
    const i32 dt = input.reduced_motion ? 0 : draw::clamp(input.elapsed, 0, 102);
    const bool held = input.held_notes != draw::notes_unavailable && input.held_notes > 0;
    const bool struck = input.note_serial != state.serial;
    state.serial = input.note_serial;

    // A note plays from Position again; the scan moves on at Speed, a share of
    // the sample's own rate, and wraps within the trim.
    if (struck) state.scan = 0;
    const i32 per_second = mul31(clamp(c.speed, -65535, 65535) << 15, static_cast<i32>(rate));  // frames
    state.scan += per_second * dt / 4;  // Q8 frames: x 256 / 1024
    const i32 span = static_cast<i32>(region << 8);  // Q8 frames
    state.scan %= span;
    if (state.scan < 0) state.scan += span;
    const u32 start = (multiply_shift(region << 8, static_cast<u32>(c.position) << 1, 32) + static_cast<u32>(state.scan)) %
                      static_cast<u32>(span);

    // Clouds' grains: a voice renders up to kGrains of them.
    const i32 life = clamp(mul31(lookup(kGrainSize, c.size), 194783), 8, 1024);  // samples / 256 / 44100 x 1024
    const i32 overlap_grains = c.overlap > 0 ? 1 + mul31(c.overlap, static_cast<i32>(kMaxGrains) - 1) : 0;
    const i32 grains = overlap_grains < static_cast<i32>(kGrains) ? overlap_grains : static_cast<i32>(kGrains);
    // A grain reads on at its Pitch against the sample's own rate, frames a second.
    const u32 ratio = exp2(octaves(c.semitones), 16);  // Q16
    const i32 reading = static_cast<i32>(multiply_shift(ratio, rate, 16));

    state.wait -= dt;
    if (struck) state.wait = 0;  // a note seeds a grain at once
    if (!held && !struck) state.wait = life;
    const i32 count = struck && !grains ? 1 : grains;
    for (u32 spawned = 0; count && state.wait <= 0 && spawned < kShownGrains; ++spawned) {
        const i32 interval = life / count > 0 ? life / count : 1;
        state.wait += c.deterministic ? interval
                                      : interval / 2 + static_cast<i32>(random_word(state.seed) >> 16) * interval / 65536;
        for (u32 i = 0; i < kShownGrains; ++i) {
            ShownGrain& g = state.grains[i];
            if (g.life) continue;
            g.frame = start;
            g.y = (kMiddle - kHalfHeight + 4 + static_cast<i32>(random_word(state.seed) >> 16) * (2 * kHalfHeight - 8) / 65536) * unit;
            g.age = 0;
            g.life = life;
            break;
        }
    }

    for (u32 i = 0; i < kShownGrains; ++i) {
        ShownGrain& g = state.grains[i];
        if (g.life) {
            g.age += dt;
            g.frame += static_cast<u32>(reading * dt / 4);
            if (g.age >= g.life || g.frame >= static_cast<u32>(span)) g.life = 0;
        }
        if (!g.life) {
            draw::hide(frame, kFirstGrain + i);
            continue;
        }
        // It swells and fades over its life, from a quarter of its 4-pixel
        // radius to all of it: the artwork's circle is the largest.
        const i32 rest = g.life - g.age, nearest = g.age < rest ? g.age : rest;
        const i32 radius = unit / 4 + 3 * unit / 2 * nearest / g.life;
        draw::place(frame, kFirstGrain + i, x_of(o.begin + (g.frame >> 8), o.frames), g.y, radius, kBright,
                    draw::fill_and_stroke);
    }
    if (held) draw::place(frame, kScan, x_of(o.begin + (start >> 8), o.frames), kMiddle * unit, unit, kMid,
                          draw::fill_and_stroke);
    else draw::hide(frame, kScan);
}
}  // namespace cirrus
