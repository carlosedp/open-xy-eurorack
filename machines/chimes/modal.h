#pragma once
#include "core.h"

// Rings' modal resonator (rings/dsp/resonator.cc) and its strike
// (Part::RenderModalVoice): a bank of band-pass filters tuned to the partials
// Structure stretches, losing Q partial by partial as Brightness falls, with
// Damping setting the decay and Position weighting the partials as a cosine.
//
// Each partial is stmlib's trapezoidal state-variable filter, rearranged for
// fixed point. Per sample:
//   t = in - RG s1 - s2;  v = HG t;  bp = s1 + v;  s1 = bp + v;  s2 += G2 bp
// with RG = g + r, HG = g / (1 + r g + g^2) and G2 = 2 g. Its poles have
// radius^2 = 1 + HG (G2 - 2 RG); computing RG as G2 / 2 + r in integers makes
// that exactly 1 - 2 r HG, so every partial is stable for any r > 0 however
// its coefficients round, and rounding only nudges pitch and decay by parts
// per million.
namespace chimes {
const i32 kHalf = 0x40000000, kQuarter = 0x20000000;  // Q31
const u32 kNyquistLimit = 1052266987u;                // 0.49, Q31
const i32 kCutoffLimit = 1071594766;                  // 0.499, Q31

// One trapezoidal filter's coefficients for g (Q28, below 4) and r (Q31).
struct Coefficients { i32 rg, hg, g, h; };
inline Coefficients coefficients(i32 g, i32 r) {
    i32 r28 = r / 8;
    if (!r28) r28 = 1;
    const u32 denominator = static_cast<u32>(kUnit + mul31(r, g) / 4 + mul31(g, g) * 2);  // 1 + r g + g^2, Q26
    const u32 h = reciprocal(denominator, 57);                                            // Q31
    Coefficients c;
    c.h = h >= 0x80000000u ? kMaximum : static_cast<i32>(h);
    c.rg = g + r28;
    c.hg = mul28(c.h, g);
    c.g = g;
    return c;
}

// One sample of a low-pass trapezoidal filter.
inline i32 low_pass(Svf& f, const Coefficients& c, i32 in) {
    const i32 v = mul31(in - mul28(f.s1, c.rg) - f.s2, c.hg);
    const i32 bp = f.s1 + v;
    f.s1 = bp + v;
    const i32 w = mul28(bp, c.g);
    const i32 lp = f.s2 + w;
    f.s2 = lp + w;
    return lp;
}

// The strike's cutoff: Brightness (2 - Brightness) sweeps eight octaves around
// the note, as in Part::Process with the internal exciter. Q31.
inline i32 strike_cutoff(u32 frequency, i32 cutoff) {
    const u32 ratio = exp2(mul31(cutoff - kHalf, 8 << 16), 16);  // 2^((cutoff - 0.5) 8), Q16
    const u32 f = multiply_shift(frequency, ratio, 17);           // Q31
    return f > static_cast<u32>(kCutoffLimit) ? kCutoffLimit : static_cast<i32>(f);
}

// Partial frequencies, Q and position weights, as Resonator::ComputeFilters.
// Recomputed only when the note or a control moves.
inline void tune_modes(Voice& voice, u32 frequency, i32 structure, i32 brightness, i32 damping) {
    if (voice.key[0] == frequency && voice.key[1] == static_cast<u32>(structure) &&
        voice.key[2] == static_cast<u32>(brightness) && voice.key[3] == static_cast<u32>(damping)) return;
    voice.key[0] = frequency;
    voice.key[1] = static_cast<u32>(structure);
    voice.key[2] = static_cast<u32>(brightness);
    voice.key[3] = static_cast<u32>(damping);
    i32 stiffness = lookup(kStiffness, structure);                // Q26
    // 500 x 10^(4 damping), Q8. Q here is 1 + partial q with the partial as a
    // share of the sample rate, so q scales by 44.1 / 48 to keep Rings' decay times.
    u32 q = exp2(mul31(damping, 870822), 8) * 459;
    // Reduce the range of brightness when structure is very low, to prevent clipping.
    i32 attenuation = kMaximum - structure;
    attenuation = mul31(attenuation, attenuation);
    attenuation = mul31(attenuation, attenuation);
    attenuation = mul31(attenuation, attenuation);
    const i32 bright = mul31(brightness, kMaximum - mul31(attenuation, 429496730));  // b (1 - 0.2 a)
    i32 loss = mul31(bend(bright), 1825361101) + 322122547;  // b (2 - b) 0.85 + 0.15
    const i32 loss_rate = mul31(bend(structure), 214748365); // s (2 - s) 0.1
    const u32 fundamental = frequency >> 4;                  // Q28
    u32 harmonic = fundamental, stretch = 1u << 26;          // Q26
    unsigned count = 0;
    for (; count < kModes; ++count) {
        const u32 partial = count ? multiply_shift(harmonic, stretch, 23) : frequency >> 1;  // Q31
        if (partial >= kNyquistLimit) break;
        const i32 g = tan_fast(static_cast<i32>(partial));
        const u32 resonance = 256 + multiply_shift(partial, q, 31);  // 1 + partial q, Q8
        const u32 r = reciprocal(resonance, 39);                    // Q31
        const Coefficients c = coefficients(g, r >= 0x80000000u ? kMaximum : static_cast<i32>(r));
        voice.rg[count] = c.rg;
        voice.hg[count] = c.hg;
        voice.g2[count] = 2 * c.g;
        stretch = static_cast<u32>(static_cast<i32>(stretch) + stiffness);
        // Partials do not fold back into negative frequencies; a few extra
        // partials reach the highest frequencies.
        stiffness = mul31(stiffness, stiffness < 0 ? 1997159793 : 2104533975);  // 0.93, 0.98
        // The highest partials do not decay too fast.
        loss += mul31(loss_rate, kMaximum - loss);
        harmonic += fundamental;
        q = multiply_shift(q, static_cast<u32>(loss), 31);
    }
    // A new partial starts from rest.
    for (unsigned i = voice.count; i < count; ++i) voice.modes[i].s1 = voice.modes[i].s2 = 0;
    voice.count = count;
}

// stmlib's approximate cosine oscillator at frequency Position, stepped once
// per partial: weights in 0..1, Q31.
inline void position_weights(i32* weights, unsigned count, i32 position) {
    i32 f = position - kQuarter, sign = 16;
    if (f < 0) f = -f;
    else if (f > kHalf) f -= kHalf;
    else sign = -16;
    const i32 coefficient = mul31(f, kMaximum - f - f) * (sign / 4);  // sign f (1 - 2 f), Q29
    i32 y1 = coefficient / 2, y0 = kQuarter;                           // Q30: iir / 4, then 0.5
    for (unsigned i = 0; i < count; ++i) {
        const i32 value = y0;
        y0 = mul31(coefficient, y0) * 4 - y1;
        y1 = value;
        const i32 weight = value + kQuarter;  // Q30, 0..1
        weights[i] = weight >= kHalf ? kMaximum : weight <= 0 ? 0 : weight * 2;
    }
}

// Runs the strike through its low-pass into the partials, adding them to out
// (Q26) as Rings sends a voice out at polyphony 4: the odd partials less the
// even (the first, third... less the second, fourth...). A strike is Rings'
// internal exciter: one pulse, 0.25 x 2^(2 fc^2) / fc, through a
// low-pass at fc with Q 1.5. That pulse is too large for fixed point, so its
// response is injected into the filter's state instead: from rest, a pulse x
// leaves s1 = 2 k and s2 = 2 g k after one sample, where k = x g h is finite.
inline void render_modal(Voice& voice, u32 frequency, i32 structure, i32 brightness, i32 damping,
                         i32 position, bool strike, i32* out) {
    tune_modes(voice, frequency, structure, mul31(brightness, brightness), damping);
    i32 targets[kModes];
    position_weights(targets, voice.count, position);

    // The strike, low-passed, at an eighth into the partials.
    const i32 cutoff = bend(brightness);
    const i32 fc = strike_cutoff(frequency, cutoff);
    const i32 ratio = tan_dirty_ratio(fc);                              // g / fc, Q28
    const Coefficients c = coefficients(mul31(ratio, fc), 1431655765);  // Q 1.5
    Svf& exciter = voice.exciter;
    if (absolute(exciter.s1) < 64 && absolute(exciter.s2) < 64) exciter.s1 = exciter.s2 = 0;
    const bool excited = strike || exciter.s1 || exciter.s2;
    i32 input[opxy::block_frames];
    if (excited) {
        for (unsigned n = 0; n < opxy::block_frames; ++n) input[n] = low_pass(exciter, c, 0);
        if (strike) {
            const i32 pulse = static_cast<i32>(exp2(mul31(mul31(fc, fc), 2 << 16), 24));  // 0.25 x 2^(2 fc^2), Q26
            // The pulse's response, superposed on the filter's at the block's first sample:
            // a second filter from rest runs the block too.
            Svf from_rest = {2 * mul31(mul28(pulse, ratio), c.h), 0};
            from_rest.s2 = 2 * mul28(from_rest.s1 / 2, c.g);
            input[0] += from_rest.s2 / 2;
            for (unsigned n = 1; n < opxy::block_frames; ++n) input[n] += low_pass(from_rest, c, 0);
            exciter.s1 += from_rest.s1;
            exciter.s2 += from_rest.s2;
        }
        for (unsigned n = 0; n < opxy::block_frames; ++n) input[n] /= 8;
    }

    // Position weights change once per block; Rings interpolates them per sample.
    for (unsigned i = 0; i < voice.count; ++i) {
        i32 s1 = voice.modes[i].s1, s2 = voice.modes[i].s2;
        const i32 rg = voice.rg[i], hg = voice.hg[i], g2 = voice.g2[i];
        const i32 weight = i & 1 ? -targets[i] : targets[i];
        if (excited) {
            for (unsigned n = 0; n < opxy::block_frames; ++n) {
                const i32 v = mul31(input[n] - mul28(s1, rg) - s2, hg);
                const i32 bp = s1 + v;
                s1 = bp + v;
                s2 += mul28(bp, g2);
                out[n] += mul31(bp, weight);
            }
        } else {
            for (unsigned n = 0; n < opxy::block_frames; ++n) {
                const i32 v = mul31(-mul28(s1, rg) - s2, hg);
                const i32 bp = s1 + v;
                s1 = bp + v;
                s2 += mul28(bp, g2);
                out[n] += mul31(bp, weight);
            }
        }
        voice.modes[i].s1 = s1;
        voice.modes[i].s2 = s2;
    }
}
}  // namespace chimes
