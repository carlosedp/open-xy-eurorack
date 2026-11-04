#include "display.h"
#include <opxy/sample_engine.hpp>
#include "granular.h"

// Cirrus' own screen over the stock sampler's page: the SDK's two-page header
// over the track's sample, its waveform from the sample's overview. A dim line
// marks where every note's scan starts, Position into the trim, and each held
// note has a playhead that moves on from there at Speed and pulses as its note
// starts. Its grains appear there as Density schedules them, glow and swell
// with their window, which Texture shapes, sit higher or lower as Spread pans
// them, and read on at their Pitch, back when the region plays reversed, with
// a trail along what they have read and the waveform lit beneath them. It is
// an impression from the controls and the notes: display code never sees the
// voices.
namespace cirrus {
namespace draw = opxy::draw;

// Nodes, in drawing order after the waveform and the header.
const u32 kWave = 0, kFirstHeader = 4;
const u32 kFirstLit = 12;                                // the waveform lit under each grain
const u32 kOrigin = kFirstLit + kShownGrains;            // where every note's scan starts
const u32 kStart = kOrigin + 1, kEnd = kStart + 1;       // the trim's ends
const u32 kFirstHead = kEnd + 1;                         // a playhead for each held note
const u32 kFirstTrail = kFirstHead + kPlayheads;         // kTrails behind each grain
const u32 kFirstGrain = kFirstTrail + kShownGrains * kTrails;
const u32 kFirstBar = kFirstGrain + kShownGrains;        // the waveform's bins, under its outline
const u32 kMessage = 16;                                 // text slot: asks for a sample
static_assert(kFirstBar + kBars == kNodes && kBars == draw::overview_bins, "Cirrus' nodes");
const i32 kLeft = 40, kWidth = 400, kMiddle = 130, kHalfHeight = 60;  // the waveform, pixels
const u32 kBinsPerPath = 24;                                           // and one shared with the next
const i32 kPulse = 256;                                                // a note's pulse, Q10 seconds
const i32 kShortestShown = 80;                                         // Q10 seconds: about five frames
// How long a released note's playhead and grains ring on, fading, while the
// amp envelope's release goes on sounding: from its Release, along a curve
// that rises 12.55 octaves from 5 ms to the 30 s the longest release rings on
// an OP-XY (stock maps its control positions to times along its own curve;
// this matches its ends), or 3 s when the envelope cannot be read. On the
// OP-XY the word falls as the release lengthens: a long release reads near 0.
inline i32 tail_of(const draw::Input& input) {
    if (!draw::envelope_known(input.amp)) return 3 * 1024;              // Q10 seconds
    const i32 length = kMaximum - input.amp.release;                    // 0 for the shortest release
    const u32 rising = exp2(mul31(length, 822477), 8);                  // 2^(12.55 length), Q8
    return clamp(static_cast<i32>(rising * 5 >> 8), 64, 32 * 1024);     // 5 ms x that, Q10 seconds
}
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

// A frame's place in the sample as 0..99, as the stock engines show a control.
inline void place_caption(char* out, u32 frame, u32 frames) {
    const unsigned t = top_bit(frames);
    const u32 fraction = multiply_shift(frame < frames ? frame : frames, reciprocal(frames, 31 + t), t);  // Q31
    caption(out, static_cast<i32>(fraction > 0x7fffffffu ? 0x7fffffffu : fraction));
}

// A frame's share of the sample, Q31, below 1: the reciprocal is good to
// 2e-6, so the last frames can round up to 1, which a Q31 word cannot hold.
inline i32 share_of(u32 frame, u32 frames) {
    const unsigned t = top_bit(frames);
    const u32 fraction = multiply_shift(frame < frames ? frame : frames - 1, reciprocal(frames, 31 + t), t);
    return static_cast<i32>(fraction > 0x7fffffffu ? 0x7fffffffu : fraction);
}
// Where a frame of the sample sits across the waveform, Q10 pixels.
inline i32 x_of(u32 frame, u32 frames) { return (kLeft + mul31(share_of(frame, frames), kWidth)) * draw::unit; }
// The overview's bin a frame falls in.
inline u32 bin_of(u32 frame, u32 frames) {
    return static_cast<u32>(mul31(share_of(frame, frames), static_cast<i32>(draw::overview_bins)));
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

// The sample's gain as the waveform's height, Q8: as tall as the sample at 0,
// the gain of a newly assigned sample, and each of stock's steps (-30..+20)
// taken as a decibel, which its range suggests; unity where it can't be read.
const i32 kLowestGain = -30, kHighestGain = 20;
const i32 kGainHeight[kHighestGain - kLowestGain + 1] = {
    8, 9, 10, 11, 13, 14, 16, 18, 20, 23, 26, 29, 32, 36, 41, 46, 51, 57, 64, 72, 81, 91, 102, 114, 128,
    144, 162, 181, 203, 228, 256, 287, 322, 362, 406, 455, 511, 573, 643, 722, 810, 908, 1019, 1144, 1283,
    1440, 1615, 1812, 2033, 2282, 2560};
inline i32 gain_scale(const draw::SampleOverview& o) {
    if (o.gain == draw::gain_unavailable) return 256;
    return kGainHeight[draw::clamp(o.gain, kLowestGain, kHighestGain) - kLowestGain];
}
// The gain as stock counts it: -30 to +20, a sign before any but 0.
inline void gain_caption(char* out, i32 gain) {
    if (gain < 0) { *out++ = '-'; gain = -gain; }
    else if (gain > 0) *out++ = '+';
    if (gain >= 10) *out++ = static_cast<char>('0' + gain / 10 % 10);
    *out++ = static_cast<char>('0' + gain % 10);
    *out = 0;
}
// A 16-bit overview value as the waveform draws it at that gain, within its height.
inline i32 scaled(i32 value, i32 scale) {
    return draw::clamp(value * scale / 256, -32768, 32767);
}

// The waveform: each path traces 25 bins along their highest values and back
// along their lowest, at least a pixel apart so that every outline has area.
inline void draw_wave(DisplayState& state, const draw::SampleOverview& o, Frame& frame, i32 scale) {
    for (u32 p = 0; p < Frame::path_count; ++p) {
        Frame::path_type& path = frame.paths[p];
        for (u32 i = 0; i <= kBinsPerPath; ++i) {
            const u32 bin = p * kBinsPerPath + i < draw::overview_bins ? p * kBinsPerPath + i : draw::overview_bins - 1;
            const i32 x = kLeft * draw::unit + static_cast<i32>(bin) * kWidth * draw::unit /
                                                   static_cast<i32>(draw::overview_bins - 1) - centre_of(p);
            i32 top = -scaled(o.high[bin], scale) * kHalfHeight * draw::unit / 32768;
            i32 bottom = -scaled(o.low[bin], scale) * kHalfHeight * draw::unit / 32768;
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
    state.scale = scale;
}

// Everything but the header and the message, out of sight.
inline void hide_body(Frame& frame) {
    for (u32 n = kFirstLit; n < kNodes; ++n) draw::hide(frame, n);
}

inline void display(DisplayState& state, const draw::Input& input, Frame& frame) {
    const i32 unit = draw::unit;

    // The header: Position, Size, Density and Texture, then Pitch, Speed,
    // Spread and Jitter; all 0..99. While Shift is held the edited row shows
    // what Shift edits on the stock sampler: the start, the end and the window
    // they make, which Shift + 3 moves, as 0..99 of the sample, and its gain,
    // in stock's steps, which the waveform's height also follows.
    const draw::SampleOverview& o = *opxy_sample_overview();
    // The trim as the voices play it: the whole sample while the region's
    // record gives no markers yet, as SampleBlock.region does then.
    const bool marked = o.end > o.begin && o.end <= o.frames;
    const u32 begin = marked ? o.begin : 0, end = marked ? o.end : o.frames;
    const char* titles[8] = {"Position", "Size", "Density", "Texture", "Pitch", "Speed", "Spread", "Jitter"};
    char values[8][draw::header_capacity] = {};
    for (u32 i = 0; i < 8; ++i) caption(values[i], input.slots[i]);
    if (input.shift) {
        u32 page = 0;
        for (u32 col = 0; col < opxy::page_controls; ++col)
            if (input.shown[col] >= opxy::page_controls) page = 1;
        const u32 first = page * opxy::page_controls;
        titles[first] = "Start";
        titles[first + 1] = "End";
        titles[first + 2] = "Window";
        titles[first + 3] = "Gain";
        const bool known = o.assigned && o.loaded && o.frames;
        place_caption(values[first], known ? begin : 0, known ? o.frames : 1);
        place_caption(values[first + 1], known ? end : 1, known ? o.frames : 1);
        place_caption(values[first + 2], known ? end - begin : 0, known ? o.frames : 1);
        if (o.gain != draw::gain_unavailable) gain_caption(values[first + 3], o.gain);
        else draw::copy(values[first + 3], draw::header_capacity, "-");  // no sample
    }
    draw::page_header(state.header, input, frame, titles, values, kFirstHeader);

    if (!state.initialized || state.track != input.track) {
        for (u32 i = 0; i < kShownGrains; ++i) state.grains[i].life = 0;
        state.seed = 1;
        state.serial = input.note_serial;
        state.generation = 0;
        for (u32 h = 0; h < kPlayheads; ++h) state.heads[h].active = 0;
        state.order = 0;
    }
    state.initialized = 1;
    state.track = input.track;

    const bool ready = o.assigned && o.loaded && o.frames >= kShortest && end > begin;
    draw::text(frame, kMessage, 200 * unit, 128 * unit, unit, !o.assigned ? "load a sample" : !ready ? "loading" : "");
    if (!ready) {
        // A frame applies whole or not at all, and an outline needs a key: until
        // the sample is ready the outlines are flat and hidden, so the header
        // and the message still show.
        if (!frame.paths[0].key) draw_wave(state, o, frame, 256);
        for (u32 p = 0; p < Frame::path_count; ++p) draw::hide(frame, kWave + p);
        hide_body(frame);
        state.generation = 0;
        return;
    }
    const i32 scale = gain_scale(o);
    if (o.generation != state.generation || scale != state.scale) {
        draw_wave(state, o, frame, scale);
        state.generation = o.generation;
    }
    for (u32 p = 0; p < Frame::path_count; ++p)
        draw::place(frame, kWave + p, centre_of(p), kMiddle * unit, unit, kLow, draw::stroke);
    // Each bin filled with a faint bar as tall as the sample is there, so the
    // waveform reads as solid behind its outline; the grains stay in front.
    for (u32 b = 0; b < kBars; ++b) {
        const i32 top = -scaled(o.high[b], scale) * kHalfHeight / 32768;  // pixels
        const i32 bottom = -scaled(o.low[b], scale) * kHalfHeight / 32768;
        const i32 half = (bottom - top) / 2 > 1 ? (bottom - top) / 2 : 1;
        draw::place(frame, kFirstBar + b,
                    kLeft * unit + static_cast<i32>(b) * kWidth * unit / static_cast<i32>(draw::overview_bins - 1),
                    (kMiddle + (top + bottom) / 2) * unit, half * unit / kHalfHeight, kDim, draw::fill_and_stroke);
    }

    // The controls as the voices read them, and the trim the grains stay in.
    Controls c = controls_of(input.slots);
    c.reverse = o.reverse;
    const u32 rate = o.rate >= 8000 && o.rate <= 192000 ? o.rate : 44100;
    const u32 region = end - begin < kFrames ? end - begin : kFrames - 1;  // as the voices read it
    const i32 dt = input.reduced_motion ? 0 : draw::clamp(input.elapsed, 0, 102);

    // Each held note plays from Position again, its scan moving on at Speed, a
    // share of the sample's own rate, and wrapping within the trim: a playhead
    // for each, up to four. As notes are released the oldest let go and fade
    // over the envelope's release; a new note takes a free playhead, else the
    // longest released, else the oldest.
    const i32 per_second = mul31(clamp(c.speed, -65535, 65535) << 15, static_cast<i32>(rate));  // frames
    const i32 span = static_cast<i32>(region << 8);                                              // Q8 frames
    const u32 origin = multiply_shift(region << 8, static_cast<u32>(c.position) << 1, 32);        // Q8 frames
    const i32 tail = tail_of(input);
    u32 started = input.note_serial - state.serial;
    if (started > kPlayheads) started = kPlayheads;
    state.serial = input.note_serial;
    for (u32 n = 0; n < started; ++n) {
        u32 slot = 0;
        for (u32 h = 0; h < kPlayheads; ++h) {
            const Playhead& a = state.heads[h];
            const Playhead& b = state.heads[slot];
            if (!a.active) {
                slot = h;
                break;
            }
            const bool a_free = a.released >= 0, b_free = b.released >= 0;
            if (a_free != b_free ? a_free : a_free ? a.released > b.released : a.serial < b.serial) slot = h;
        }
        Playhead& head = state.heads[slot];
        head.active = 1;
        head.serial = ++state.order;
        head.scan = 0;
        head.wait = 0;  // a note seeds a grain at once
        head.seeded = 0;
        head.age = 0;
        head.released = -1;
    }
    u32 held = 0;
    for (u32 h = 0; h < kPlayheads; ++h) held += state.heads[h].active && state.heads[h].released < 0;
    const u32 notes = input.held_notes == draw::notes_unavailable ? held : input.held_notes;
    while (held > notes) {
        u32 oldest = kPlayheads;
        for (u32 h = 0; h < kPlayheads; ++h) {
            const Playhead& a = state.heads[h];
            if (a.active && a.released < 0 && (oldest == kPlayheads || a.serial < state.heads[oldest].serial)) oldest = h;
        }
        state.heads[oldest].released = 0;
        --held;
    }

    // Clouds' grains: a voice renders up to kGrains of them, each at its own pan.
    const i32 life = clamp(mul31(lookup(kGrainSize, c.size), 194783), 8, 1024);  // samples / 256 / 44100 x 1024
    const i32 overlap_grains = c.overlap > 0 ? 1 + mul31(c.overlap, static_cast<i32>(kMaxGrains) - 1) : 0;
    const i32 grains = overlap_grains < static_cast<i32>(kGrains) ? overlap_grains : static_cast<i32>(kGrains);
    // A grain reads on at its Pitch against the sample's own rate, frames a
    // second, back when the region plays reversed; Jitter spreads it, as each
    // voice's grains are detuned.
    // A grain shorter than a few frames would be drawn at whatever point of its
    // window a frame lands on, big and bright at some sizes and faint at
    // others: on screen it lives at least kShortestShown, its window spread
    // over that, and its peak size follows its real length.
    const i32 shown = life > kShortestShown ? life : kShortestShown;
    const i32 peak = unit * 2 / 5 + unit * 3 / 5 * (life < 512 ? life : 512) / 512;  // its largest scale
    const u32 ratio = exp2(octaves(c.semitones), 16);  // Q16
    const i32 reading = static_cast<i32>(multiply_shift(ratio, rate, 16));
    // Its window as the voices shape it (start_grain), for its glow.
    const bool smooth = c.window_shape >= 1 << 30;
    const i32 smoothness = smooth ? (c.window_shape - (1 << 30)) >> 15 : 0;
    const i32 slope = smooth ? 0 : static_cast<i32>(reciprocal(static_cast<u32>(c.window_shape + 21474836), 40));

    for (u32 h = 0; h < kPlayheads; ++h) {
        Playhead& head = state.heads[h];
        if (head.active && head.released >= 0) {
            head.released += dt;
            if (head.released >= tail) head.active = 0;
        }
        if (!head.active) {
            draw::hide(frame, kFirstHead + h);
            continue;
        }
        const i32 level = head.released < 0 ? 32767 : (tail - head.released) * 32767 / tail;  // Q15
        head.scan = (head.scan + per_second * dt / 4) % span;  // Q8 frames: x 256 / 1024
        if (head.scan < 0) head.scan += span;
        const u32 start = (origin + static_cast<u32>(head.scan)) % static_cast<u32>(span);
        head.wait -= dt;
        // At Density's middle a note plays its own grain alone.
        const i32 count = grains ? grains : head.seeded ? 0 : 1;
        for (u32 spawned = 0; count && head.wait <= 0 && spawned < kShownGrains; ++spawned) {
            const i32 interval = shown / count > 0 ? shown / count : 1;
            head.wait += c.deterministic ? interval
                                         : interval / 2 + static_cast<i32>(random_word(state.seed) >> 16) * interval / 65536;
            head.seeded = 1;
            for (u32 i = 0; i < kShownGrains; ++i) {
                ShownGrain& g = state.grains[i];
                if (g.life) continue;
                // A reversed grain starts at the far end of what it reads.
                const u32 eaten = static_cast<u32>(reading / 4) * static_cast<u32>(life);  // Q8 frames
                g.frame = g.origin = c.reverse ? (start + eaten < static_cast<u32>(span) ? start + eaten : start) : start;
                // Up is left, down is right: Spread pans each grain at random about the centre.
                g.y = kMiddle * unit + mul31(mul31(c.spread, noise_of(state.seed)), (kHalfHeight - 8) * unit);
                g.age = 0;
                g.life = shown;
                g.level = level;
                break;
            }
        }
        // A note's pulse: its playhead starts half again as large and bright,
        // and settles over a quarter second.
        const i32 pulse = head.age < kPulse ? kPulse - head.age : 0;
        head.age += dt;
        draw::place(frame, kFirstHead + h, x_of(begin + (start >> 8), o.frames), kMiddle * unit,
                    unit + unit / 2 * pulse / kPulse,
                    pulse ? kWhite : head.released < 0 ? kBright : level > 21845 ? kMid : level > 10923 ? kLow : kDim,
                    draw::fill_and_stroke);
    }

    for (u32 i = 0; i < kShownGrains; ++i) {
        ShownGrain& g = state.grains[i];
        if (g.life) {
            g.age += dt;
            const u32 moved = static_cast<u32>(reading * dt / 4);
            g.frame = c.reverse ? g.frame - moved : g.frame + moved;
            if (g.age >= g.life || g.frame >= static_cast<u32>(span)) g.life = 0;
        }
        if (!g.life) {
            draw::hide(frame, kFirstGrain + i);
            draw::hide(frame, kFirstLit + i);
            for (u32 k = 0; k < kTrails; ++k) draw::hide(frame, kFirstTrail + i * kTrails + k);
            continue;
        }
        // Its window's gain now, as the voices render it: its glow and its
        // size, a quarter of its 8-pixel radius to all of it.
        const u32 envelope = static_cast<u32>(g.age) * (0x80000000u / static_cast<u32>(g.life));
        // A released note's grains ring on smaller and dimmer as it fades.
        const i32 gain = (smooth ? gain_at<true, true>(envelope, smoothness, slope)
                                 : gain_at<true, false>(envelope, smoothness, slope)) * g.level >> 15;  // Q15
        const i32 x = x_of(begin + (g.frame >> 8), o.frames);
        draw::place(frame, kFirstGrain + i, x, g.y, unit / 5 + (peak - unit / 5) * gain / 32768,
                    kMid + static_cast<u32>(3 * gain / 32768), draw::fill_and_stroke);
        // Its trail: dots back along what it has read, dimmer and smaller.
        const i32 read = static_cast<i32>(g.frame - g.origin);
        for (u32 k = 0; k < kTrails; ++k) {
            const u32 behind = g.frame - static_cast<u32>(read / 3 * static_cast<i32>(k + 1));
            draw::place(frame, kFirstTrail + i * kTrails + k, x_of(begin + (behind >> 8), o.frames), g.y,
                        unit / 4 * static_cast<i32>(kTrails - k) / static_cast<i32>(kTrails) + unit / 8,
                        k ? kDim : kLow, draw::fill_and_stroke);
        }
        // The waveform under it, lit as tall as the sample is there.
        const u32 bin = bin_of(begin + (g.frame >> 8), o.frames);
        const i32 top = -scaled(o.high[bin], scale) * kHalfHeight / 32768;  // pixels
        const i32 bottom = -scaled(o.low[bin], scale) * kHalfHeight / 32768;
        const i32 half = (bottom - top) / 2 > 1 ? (bottom - top) / 2 : 1;
        draw::place(frame, kFirstLit + i, x, (kMiddle + (top + bottom) / 2) * unit, half * unit / kHalfHeight,
                    kDim + static_cast<u32>(3 * gain / 32768), draw::fill_and_stroke);
    }
    // Where every note's scan starts: the trim's start and Position, dim.
    draw::place(frame, kOrigin, x_of(begin + (origin >> 8), o.frames), kMiddle * unit, unit, kDim, draw::fill_and_stroke);
    // The trim's ends, which Shift's first two encoders move.
    draw::place(frame, kStart, x_of(begin, o.frames), kMiddle * unit, unit, kBright, draw::fill_and_stroke);
    draw::place(frame, kEnd, x_of(end - 1, o.frames), kMiddle * unit, unit, kBright, draw::fill_and_stroke);
}
}  // namespace cirrus
