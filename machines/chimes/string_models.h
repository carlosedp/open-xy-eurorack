#pragma once
#include "modal.h"

// Rings' strings (rings/dsp/string.cc), their plucker (plucker.h) and the
// sympathetic tunings and chords of Part::RenderStringVoice, in fixed point.
// A string is a delay line one period long, damped each pass by an FIR
// (Brightness) and an IIR low-pass, whose delays the line gives back to stay in
// tune. A second tap at the pickup position combs the output. The string model
// adds Rings' dispersion: an all-pass "stiffness" line, and at the ends of
// Structure a noisy delay or a curved bridge.
namespace chimes {
// What one block asks of a string.
struct StringControls {
    u32 frequency;  // Q32
    i32 glide;      // share of the way to the frequency per block, Q31; kMaximum jumps there
    i32 brightness, damping, position;  // Q31
};
// The string model's dispersion, from Structure.
struct Dispersion {
    i32 stretch_point, noise_amount, curving, ac_blocking, ap_gain;  // Q31
};


// The per-block settings of one string: String::ProcessInternal's preamble.
struct StringBlock {
    u32 period, comb;    // the loop's and the pickup's delays, Q16 samples
    i32 fir0, fir1;      // FIR weights: damping (1 + b) / 2, damping (1 - b) / 4, Q31
    Coefficients iir;    // the IIR damping low-pass, Q 0.5
    i32 noise_filter;    // the dispersion noise's low-pass, Q31
};
inline StringBlock prepare_string(String& s, const StringControls& k) {
    if (!s.period || k.glide >= kMaximum) s.frequency = k.frequency;
    else s.frequency += static_cast<u32>(mul31(as_signed(k.frequency - s.frequency), k.glide));
    // One period, within the line. Below 21.5 Hz the string plays its lowest
    // note: Rings' upsampler for those is not ported.
    u32 delay = reciprocal(s.frequency, 48);
    if (delay < 4u << 16) delay = 4u << 16;
    if (delay > (kLine - 4) << 16) delay = (kLine - 4) << 16;
    const i32 position = kHalf - mul31(absolute(k.position - kHalf), 2104533975);  // 0.5 - 0.98 |p - 0.5|

    // Decay: the loop's gain per period for an RT60 of 0.07 x 2^(8 lf) seconds.
    const i32 lf = bend(k.damping);
    const i32 shorter = clamp31(exp2(-mul31(lf, 8 << 16), 31));                     // 2^(-8 lf)
    i32 octaves_per_pass = -mul31(mul31(static_cast<i32>(delay), shorter), 6956539);  // -10 delay / rt60
    if (octaves_per_pass < -693589) octaves_per_pass = -693589;                    // -127 semitones
    i32 coefficient = clamp31(exp2(octaves_per_pass, 31));
    i32 brightness = mul31(k.brightness, k.brightness);
    // The IIR's cutoff, in semitones above the note (Q16), then as a frequency.
    i32 cutoff = (24 << 16) + mul31(mul31(k.damping, k.damping), 48 << 16) + mul31(brightness, 24 << 16);
    if (cutoff > 84 << 16) cutoff = 84 << 16;
    const u32 f = multiply_shift(s.frequency, exp2(octaves(cutoff), 16), 17);  // Q31
    i32 damping_f = f > static_cast<u32>(kCutoffLimit) ? kCutoffLimit : static_cast<i32>(f);
    // Crossfade to infinite decay.
    if (k.damping >= 2040109466) {  // 0.95
        const i32 t = (k.damping - 2040109466) * 20;
        coefficient += mul31(t, kMaximum - coefficient);
        brightness += mul31(t, kMaximum - brightness);
        damping_f += mul31(t, 1073527076 - damping_f);  // 0.4999
    }
    StringBlock b;
    b.fir0 = mul31(coefficient, kHalf + brightness / 2);
    b.fir1 = mul31(coefficient, (kMaximum - brightness) / 4);
    b.iir.g = tan_accurate(damping_f);
    b.iir.rg = b.iir.g + 2 * kOne28;
    const i32 one_plus_g = kOne28 + b.iir.g;
    b.iir.h = clamp31(reciprocal(static_cast<u32>(mul31(one_plus_g, one_plus_g)), 56));  // 1 / (1 + g)^2
    b.iir.hg = mul28(b.iir.h, b.iir.g);
    b.noise_filter = clamp31(exp2(mul31(k.brightness - kMaximum, 4 << 16), 31));    // 2^((b - 1) 4)
    // The line gives back the FIR's one sample and the IIR's delay at the note,
    // atan(x) / pi periods for x = tan(pi f) / g. Rings looks the delay up by the
    // cutoff's ratio to the note, which near the top of the range overlooks the
    // filter's frequency warping and tunes high notes sharp.
    const i32 t = tan_accurate(static_cast<i32>(s.frequency >> 1)), g = b.iir.g;
    i32 x = kMaximum;
    if (t < g) {
        const unsigned scale = 30 - top_bit(static_cast<u32>(g));  // g to 2^30..2^31
        x = static_cast<i32>(multiply_shift(static_cast<u32>(t) << scale,
                                            reciprocal(static_cast<u32>(g) << scale, 61), 30));
    }
    const i32 compensation = kMaximum - lookup(kArctan, x);
    b.period = static_cast<u32>(mul31(static_cast<i32>(delay), compensation)) - 65536;
    b.comb = static_cast<u32>(mul31(static_cast<i32>(delay), position));
    s.period = b.period;
    s.comb = b.comb;
    return b;
}

// A fractional delay of 1..2 samples: Q16 `rest` as the first-order all-pass
// coefficient (1 - d) / (1 + d), -1/3..0, Q31.
inline i32 allpass_coefficient(u32 rest) {
    return -static_cast<i32>((((rest - 65536) << 14) / ((rest + 65536) >> 1)) << 16);
}

// Adds the pickup, `comb` (Q16) behind write position w at the block's start,
// to out (Q26): a pickup reads at most the sample just written, so it can
// follow the whole block's writes.
inline void add_pickup(const i32* line, u32 w, u32 comb, i32* out) {
    const i32 fraction = static_cast<i32>((comb & 0xffffu) << 15);
    u32 tap = w + 1 - (comb >> 16);
    for (unsigned i = 0; i < opxy::block_frames; ++i, ++tap) {
        const i32 a = line[tap & kLineMask];
        out[i] += a + mul31(line[(tap - 1) & kLineMask] - a, fraction);
    }
}

// The steady loop: whole samples read back, an all-pass for the last 1..2,
// optionally the stiffness all-pass `stiffness` whole samples long, the input,
// then the damping filters. Writes the string to line and to out (Q26).
// Rings reads the loop with Hermite interpolation each sample; with the delay
// fixed for the block, an all-pass tunes it with one multiply and a flat
// magnitude response, as Karplus does.
//
// Done in one loop, the steps need more values than the Blackfin has
// registers, and the loop reloads them from the stack every sample. So each
// step is a pass of its own: the read and all-pass, the stiffness line, the
// input and FIR, the IIR, then the writes. A pass may only read what earlier
// passes wrote, so the block goes in chunks no longer than the loop's (and the
// stiffness line's) delay, which the line always holds already. The output is
// the same as one loop's, sample for sample.
template <bool stiff>
__attribute__((noinline)) void run_string(String& s, const StringBlock& b, u32 delay, i32* __restrict line,
                                          i32* __restrict stretch, u32 stiffness, i32 ap_gain, u32 w,
                                          const i32* __restrict in, i32* __restrict out) {
    const u32 whole = (delay >> 16) - 1;  // at least 3
    const i32 coefficient = allpass_coefficient(delay - (whole << 16));
    u32 chunk = whole < opxy::block_frames ? whole : opxy::block_frames;
    if (stiff && stiffness < chunk) chunk = stiffness;
    i32 ap_in = s.ap_in, ap_out = s.ap_out, x1 = s.x1, x2 = s.x2;
    Svf filter = s.iir;
    i32 samples[opxy::block_frames];
    for (u32 start = 0; start < opxy::block_frames; start += chunk) {
        const u32 n = opxy::block_frames - start < chunk ? opxy::block_frames - start : chunk;
        i32* __restrict sample = samples + start;
        u32 read = w - whole;
        for (u32 i = 0; i < n; ++i) {
            const i32 x = line[read++ & kLineMask];
            const i32 y = ap_in + mul31(coefficient, x - ap_out);
            ap_in = x;
            ap_out = y;
            sample[i] = y;
        }
        if (stiff) {
            // stmlib's Allpass over the stiffness line.
            u32 at = w;
            for (u32 i = 0; i < n; ++i, ++at) {
                const i32 back = stretch[(at - stiffness) & kStretchMask];
                const i32 written = sample[i] + mul31(back, ap_gain);
                stretch[at & kStretchMask] = written;
                sample[i] = back - mul31(written, ap_gain);
            }
        }
        const i32 fir0 = b.fir0, fir1 = b.fir1;
        for (u32 i = 0; i < n; ++i) {
            const i32 x = sample[i] + in[start + i];
            sample[i] = mul31(fir0, x1) + mul31(fir1, x + x2);
            x2 = x1;
            x1 = x;
        }
        const Coefficients c = b.iir;
        for (u32 i = 0; i < n; ++i) out[start + i] = low_pass(filter, c, sample[i]);
        for (u32 i = 0; i < n; ++i, ++w) line[w & kLineMask] = out[start + i];
    }
    s.ap_in = ap_in;
    s.ap_out = ap_out;
    s.x1 = x1;
    s.x2 = x2;
    s.iir = filter;
}

// The largest magnitude in a block.
inline i32 block_peak(const i32* x) {
    i32 peak = 0;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 a = absolute(x[i]);
        if (a > peak) peak = a;
    }
    return peak;
}
// One plain string for one block, adding the string to out and its pickup to
// aux (Q26).
inline void render_string(String& s, i32* line, u32 w, const StringControls& k,
                          const i32* in, i32* out, i32* aux) {
    const StringBlock b = prepare_string(s, k);
    i32 string[opxy::block_frames];
    run_string<false>(s, b, b.period, line, nullptr, 0, 0, w, in, string);
    for (unsigned i = 0; i < opxy::block_frames; ++i) out[i] += string[i];
    add_pickup(line, w, b.comb, aux);
}

// The string model's string, with dispersion; `stretch` is the stiffness line.
// Between Structure 0.26 and about 0.82 the delay holds through the block and
// splits between the loop and the stiffness all-pass. Toward either end it
// moves every sample, with noise above or the curved bridge below; there the
// loop is read with linear interpolation, where Rings uses Hermite.
__attribute__((noinline)) void render_dispersed_string(String& s, i32* __restrict line, i32* __restrict stretch,
                                                       u32 w, const StringControls& k, const Dispersion& d, u32& seed,
                                                       const i32* __restrict in, i32* out, i32* aux) {
    const StringBlock b = prepare_string(s, k);
    i32 string[opxy::block_frames];
    if (!d.noise_amount && !d.curving) {
        const i32 ap = mul31(static_cast<i32>(b.period), d.stretch_point);
        const i32 main = static_cast<i32>(b.period) - ap;
        if (ap >= 4 << 16 && main >= 4 << 16)
            run_string<true>(s, b, static_cast<u32>(main), line, stretch, static_cast<u32>(ap >> 16), d.ap_gain, w, in,
                             string);
        else
            run_string<false>(s, b, b.period, line, nullptr, 0, 0, w, in, string);
    } else {
        const i32 noise_gain = static_cast<i32>(reciprocal(static_cast<u32>(429496730 + b.noise_filter), 59));  // 1 / (0.2 + nf), Q28
        u32 random = seed, at = w;
        i32 x1 = s.x1, x2 = s.x2, noise = s.noise, bridge = s.bridge;
        DcBlocker coupling = s.coupling;
        Svf filter = s.iir;
        for (unsigned i = 0; i < opxy::block_frames; ++i, ++at) {
            noise += mul31(mul28(noise_of(random) / 32, noise_gain) - noise, b.noise_filter);
            // The noise and the bridge bend the delay: 1 + noise amount - bridge curving, Q30.
            const i32 fm = kHalf + (mul31(noise, d.noise_amount) - mul31(bridge, d.curving)) * 16;
            i32 delay = mul31(static_cast<i32>(b.period), fm) * 2;
            const i32 ap = mul31(delay, d.stretch_point), main = delay - ap;
            const bool split = ap >= 4 << 16 && main >= 4 << 16;
            if (split) delay = main;
            if (delay < 2 << 16) delay = 2 << 16;
            const u32 tap = at - (static_cast<u32>(delay) >> 16);
            const i32 a = line[tap & kLineMask];
            i32 sample = a + mul31(line[(tap - 1) & kLineMask] - a, (delay & 0xffff) << 15);
            if (split) {
                const i32 back = stretch[(at - static_cast<u32>(ap >> 16)) & kStretchMask];
                const i32 written = sample + mul31(back, d.ap_gain);
                stretch[at & kStretchMask] = written;
                sample = back - mul31(written, d.ap_gain);
            }
            // A curved bridge, AC-coupled as the bridge curves.
            sample += mul31(coupling.process<11>(sample) - sample, d.ac_blocking);
            const i32 over = absolute(sample) - 1677722;  // 0.025
            bridge = sample > 0 ? absolute(over) + over : -((absolute(over) + over) * 3 / 2);
            sample += in[i];
            const i32 fir = mul31(b.fir0, x1) + mul31(b.fir1, sample + x2);
            x2 = x1;
            x1 = sample;
            const i32 lp = low_pass(filter, b.iir, fir);
            string[i] = lp;
            line[at & kLineMask] = lp;
        }
        seed = random;
        s.x1 = x1;
        s.x2 = x2;
        s.noise = noise;
        s.bridge = bridge;
        s.coupling = coupling;
        s.iir = filter;
    }
    for (unsigned i = 0; i < opxy::block_frames; ++i) out[i] += string[i];
    add_pickup(line, w, b.comb, aux);
}

// Plucker::Trigger: a noise burst a fraction of a period long by Position,
// combed by Position and low-passed at `cutoff` (Q31).
inline void trigger_pluck(Voice& voice, u32 frequency, i32 cutoff, i32 position) {
    const i32 ratio = mul31(position, 1932735283) + 107374182;  // 0.9 p + 0.05
    u32 period = static_cast<u32>(mul31(clamp31(reciprocal(frequency, 48)), ratio));  // Q16 samples
    voice.pluck_remaining = period >> 16;
    while (period >= 255u << 16) period >>= 1;
    voice.pluck_period = period;
    // Its echoes fall by at least the comb's gain, 0.8, each period: 80 dB in 42.
    voice.pluck_ringing = (42 * (period >> 16) + 63) / 64 + 2;
    voice.pluck_gain = mul31(kMaximum - position, 1717986918);  // (1 - p) 0.8
    const Coefficients c = coefficients(mul31(tan_dirty_ratio(cutoff), cutoff), kMaximum);  // Q 1
    voice.pluck_rg = c.rg;
    voice.pluck_hg = c.hg;
    voice.pluck_g = c.g;
}
// Plucker::Process, then the exciter's DC blocker: the strings' input, Q26.
// Once its echoes have died away only the DC blocker runs, returning the
// burst's offset as it settles (the string would otherwise keep it), and then
// it rests, and the input is silence. Out of line, for its own registers.
__attribute__((noinline)) void render_pluck(Voice& voice, i32* __restrict comb, u32 w, i32* __restrict out) {
    if (!voice.pluck_remaining) {
        if (!voice.pluck_ringing) {
            DcBlocker& blocker = voice.blocker;
            if (blocker.y || blocker.x) {
                for (unsigned i = 0; i < opxy::block_frames; ++i) out[i] = blocker.process<12>(0);
            } else {
                for (unsigned i = 0; i < opxy::block_frames; ++i) out[i] = 0;
            }
            return;
        }
        --voice.pluck_ringing;
    }
    Coefficients c;
    c.rg = voice.pluck_rg;
    c.hg = voice.pluck_hg;
    c.g = voice.pluck_g;
    const u32 whole = voice.pluck_period >> 16;
    const i32 fraction = static_cast<i32>((voice.pluck_period & 0xffffu) << 15), gain = voice.pluck_gain;
    u32 remaining = voice.pluck_remaining, seed = voice.seed;
    Svf filter = voice.pluck_filter;
    DcBlocker blocker = voice.blocker;
    for (unsigned i = 0; i < opxy::block_frames; ++i, ++w) {
        i32 x = 0;
        if (remaining) {
            x = noise_of(seed) / 32;
            --remaining;
        }
        const i32 a = comb[(w - whole) & kPluckMask];
        const i32 y = x + mul31(a + mul31(comb[(w - whole - 1) & kPluckMask] - a, fraction), gain);
        comb[w & kPluckMask] = y;
        out[i] = blocker.process<12>(low_pass(filter, c, y));
    }
    voice.pluck_remaining = remaining;
    voice.seed = seed;
    voice.pluck_filter = filter;
    voice.blocker = blocker;
}

// Part::ComputeSympatheticStringsNotes for two strings: the sympathetic
// string's offset from the note, in semitones (Q16), between the notes Rings
// offers by Structure. Rings' tonic is its FREQUENCY knob; with the note from
// the keys it is the note itself, as on Rings with nothing patched to V/OCT.
const i32 kSympatheticNotes[9] = {0, -786432, -460033, 0, 460033, 786432, 1246465, 1572864, 1572864};
// Part::Squash: an S-curve that dwells on the notes.
inline i32 squash(i32 x) {
    const bool low = x < kHalf;
    i32 y = low ? x * 2 : (kMaximum - x) * 2;
    for (unsigned i = 0; i < 4; ++i) y = mul31(y, y);
    return low ? y / 2 : kMaximum - y / 2;
}
inline i32 sympathetic_offset(i32 structure) {
    const i32 note = (structure / 8) * 7;  // Q28, 0..7
    const i32 whole = note >> 28, fraction = squash((note & ((1 << 28) - 1)) * 8);
    const i32 a = kSympatheticNotes[whole];
    return a + mul31(kSympatheticNotes[whole + 1] - a, fraction);
}
inline u32 transpose(u32 frequency, i32 semitones) {
    return multiply_shift(frequency, exp2(octaves(semitones), 28), 28);
}
// Rings' pickup LFOs for the second strings of its four voices: 0.211, 0.2,
// 0.171 and 0 Hz, as phase steps per block.
const u32 kLfo[4] = {1315175, 1246612, 1065853, 0};

// The string, sympathetic and chord models: Part::RenderStringVoice with the
// internal exciter. `voice_index` is this voice's place among Rings' four,
// which sets its sympathetic string's pickup modulation.
inline void render_strings(Voice& voice, Lines& lines, u32 model, u32 voice_index, u32 frequency, i32 structure,
                           i32 brightness, i32 damping, i32 position, bool strum, i32* out, i32* aux) {
    if (strum) {
        const i32 fc = strike_cutoff(frequency, bend(brightness));
        trigger_pluck(voice, frequency, fc >= kCutoffLimit / 8 ? kCutoffLimit : fc * 8, position);
    }
    i32 input[opxy::block_frames];
    render_pluck(voice, lines.pluck, voice.write, input);

    i32 offsets[kStrings] = {0, 0};
    if (model == kSympathetic) offsets[1] = sympathetic_offset(structure);
    if (model == kChord) {
        const u32 chord = ((static_cast<u32>(structure) >> 8) * 10 + (1u << 22)) >> 23;  // round(10 s)
        offsets[0] = kChords[chord][0];
        offsets[1] = kChords[chord][1];
    }
    const StringControls first = {transpose(frequency, offsets[0]), kMaximum, brightness, damping, position};
    if (model == kString) {
        Dispersion d = {0, 0, 0, 0, 0};
        // -1 below Structure 0.24, rising from 0 above 0.26.
        const i32 amount = structure < 515396076 ? mul28(structure - 515396076, 1118302110)  // 4.166
                         : structure > 558345748 ? mul28(structure - 558345748, 362750253)   // 1.35135
                         : 0;
        if (amount > 0) d.stretch_point = mul31(bend(amount), 1020054733);  // 0.475
        if (amount > 1610612736) {  // 0.75
            const i32 x = (amount - 1610612736) * 4;
            d.noise_amount = mul31(mul31(x, x), 53687091);  // x^2 0.025
        }
        if (amount < 0) {
            d.ac_blocking = -amount;
            d.curving = mul31(mul31(amount, amount), 21474836);  // 0.01
        }
        // -0.618 d / (0.15 + |d|)
        const i32 inverse = static_cast<i32>(reciprocal(static_cast<u32>(322122547 + absolute(amount)), 59));  // Q28
        d.ap_gain = -mul28(mul31(amount, 1327144894), inverse);
        render_dispersed_string(voice.strings[0], lines.strings[0], lines.strings[1], voice.write, first, d,
                                voice.seed, input, out, aux);
    } else {
        render_string(voice.strings[0], lines.strings[0], voice.write, first, input, out, aux);
        // The second string rings by sympathy with the first.
        i32 sympathy[opxy::block_frames];
        for (unsigned i = 0; i < opxy::block_frames; ++i) sympathy[i] = mul31(out[i] - aux[i], 214748365);  // 0.2 / 2
        const i32 bright = bend(bend(brightness));
        const i32 base = 1503238554 + mul31(damping, 579820585);                   // 0.7 + 0.27 d
        const i32 amount = mul31(kHalf - absolute(kHalf - position), 1932735283);  // (0.5 - |0.5 - p|) 0.9
        // Rings glides the sympathetic strings by 2^((b - 1) 3) per 24-sample
        // block at 48 kHz: about three of those per 64 samples at 44.1 kHz.
        const i32 stay = kMaximum - clamp31(exp2(mul31(bright - kMaximum, 3 << 16), 31));
        const i32 glide = kMaximum - mul31(mul31(stay, stay), stay);
        String& s = voice.strings[1];
        s.lfo += kLfo[voice_index & 3];
        const i32 lfo = sine(s.lfo + 0x40000000u) / 2 + kHalf;  // 0.5 + 0.5 cos, 0..1
        // Half of the way from base to 0.95.
        const StringControls k = {transpose(frequency, offsets[1]), glide, bright,
                                  base + mul31(2040109466 - base, kHalf), position + mul31(lfo, amount)};
        render_string(s, lines.strings[1], voice.write, k, sympathy, out, aux);
    }
    voice.write += opxy::block_frames;
}
}  // namespace chimes
