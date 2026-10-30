#pragma once
#include <opxy/q31.hpp>
#include "tables.h"

// Fixed-point arithmetic for Chimes. The Blackfin has no floating point and the
// firmware links no 64-bit or float helpers, so every Rings formula is rebuilt
// from 32-bit integer operations here.
//
// Formats used throughout:
//   audio        Q26: Rings' full scale (1.0) is 2^26, leaving 32x headroom
//   coefficients Q31 below 1, Q28 below 8
//   frequency    Q32 unsigned: cycles per sample, the OP-XY phase increment
//   controls     Q31, 0..1
//   pitch        Q16 semitones or octaves
namespace chimes {
using opxy::i32;
using opxy::u32;

const i32 kUnit = 1 << 26;               // 1.0 in Q26 audio
const i32 kRest = 64;                    // -120 dB of kUnit: below it a partial rests
const i32 kLife = 24 << 24;              // 24 octaves (-144 dB) a partial rings at most, Q24
const i32 kOne28 = 1 << 28;              // 1.0 in Q28
const i32 kMaximum = opxy::q31::maximum; // just below 1.0 in Q31

// a * b for Q31 b.
inline i32 mul31(i32 a, i32 b) { return opxy::q31::multiply(a, b); }
// a * c for Q28 c (|c| < 8); the product must fit.
inline i32 mul28(i32 a, i32 c) { return opxy::q31::multiply(a, c) * 8; }

inline i32 clamp(i32 value, i32 low, i32 high) { return value < low ? low : value > high ? high : value; }
// An unsigned Q31 result as a signed one, saturating 1.0.
inline i32 clamp31(u32 value) { return value >= 0x80000000u ? kMaximum : static_cast<i32>(value); }
inline i32 absolute(i32 value) { return value < 0 ? -value : value; }
// A Q31 control, negative words read as 0.
inline i32 control(i32 word) { return word < 0 ? 0 : word; }
// x (2 - x) for Q31 x in 0..1: Rings' favourite curve.
inline i32 bend(i32 x) { return x + mul31(x, kMaximum - x); }
// Floor division by 2^shift for signed values, without implementation-defined shifts.
inline i32 floor_shift(i32 value, unsigned shift) {
    const i32 scale = static_cast<i32>(1u << shift);
    return value >= 0 ? value / scale : -((-value + scale - 1) / scale);
}

// Unsigned 32 x 32 -> 64-bit product, as two words.
struct Wide { u32 hi, lo; };
inline Wide wide_multiply(u32 a, u32 b) {
    const u32 al = a & 0xffffu, ah = a >> 16, bl = b & 0xffffu, bh = b >> 16;
    const u32 ll = al * bl, lh = al * bh, hl = ah * bl;
    const u32 middle = (ll >> 16) + (lh & 0xffffu) + (hl & 0xffffu);
    Wide w;
    w.lo = (middle << 16) | (ll & 0xffffu);
    w.hi = ah * bh + (lh >> 16) + (hl >> 16) + (middle >> 16);
    return w;
}
// (a * b) >> shift for 1 <= shift <= 63, saturated to 32 bits.
inline u32 multiply_shift(u32 a, u32 b, unsigned shift) {
    const Wide w = wide_multiply(a, b);
    if (shift >= 32) return w.hi >> (shift - 32);
    if (w.hi >> shift) return 0xffffffffu;
    return (w.hi << (32 - shift)) | (w.lo >> shift);
}

// Index of the highest set bit; value > 0.
inline unsigned top_bit(u32 value) {
    unsigned n = 0;
    if (value >> 16) { value >>= 16; n += 16; }
    if (value >> 8) { value >>= 8; n += 8; }
    if (value >> 4) { value >>= 4; n += 4; }
    if (value >> 2) { value >>= 2; n += 2; }
    if (value >> 1) n += 1;
    return n;
}

// Shifts left (positive) or right (negative), saturating to 32 bits.
inline u32 shift_saturate(u32 value, i32 shift) {
    if (shift >= 0) {
        if (shift >= 32) return value ? 0xffffffffu : 0;
        if (shift && (value >> (32 - shift))) return 0xffffffffu;
        return value << shift;
    }
    return -shift >= 32 ? 0 : value >> -shift;
}

// table[i] + (table[i + 1] - table[i]) * fraction / 256, fraction 0..255.
// Safe for any difference that fits in 32 bits.
inline i32 lerp(const i32* table, u32 i, u32 fraction) {
    const i32 a = table[i], difference = table[i + 1] - a, f = static_cast<i32>(fraction);
    return a + (difference / 256) * f + (difference % 256) * f / 256;
}
inline u32 lerp(const u32* table, u32 i, u32 fraction) {
    const u32 a = table[i], difference = table[i + 1] - a;  // increasing tables
    return a + (difference >> 8) * fraction + ((difference & 255u) * fraction >> 8);
}
// A 257-entry table spanning a Q31 control 0..1.
inline i32 lookup(const i32* table, i32 control_q31) {
    const u32 x = static_cast<u32>(control(control_q31));
    return lerp(table, x >> 23, (x >> 15) & 255u);
}

// 2^(octaves) in Q(bits), octaves in Q16; saturated to 32 bits. About 1e-6 accurate.
inline u32 exp2(i32 octaves, unsigned bits) {
    const i32 whole = floor_shift(octaves, 16);
    const u32 part = static_cast<u32>(octaves - whole * 65536);  // 0..65535
    const u32 mantissa = lerp(kPow2, part >> 8, part & 255u);    // Q30, 1..2
    return shift_saturate(mantissa, whole + static_cast<i32>(bits) - 30);
}
// Semitones to octaves, both Q16: x / 12.
inline i32 octaves(i32 semitones) { return mul31(semitones, 178956971); }

// 2^power / value, saturated to 32 bits; value > 0. About 2e-6 accurate: a
// 1/(1 + k/256) table on the normalized mantissa.
inline u32 reciprocal(u32 value, unsigned power) {
    const unsigned top = top_bit(value);
    // The 20 bits below the top bit: an 8-bit index and a 12-bit fraction.
    const u32 below = top >= 20 ? (value >> (top - 20)) & 0xfffffu : (value << (20 - top)) & 0xfffffu;
    const u32 i = below >> 12, f = below & 4095u;
    const u32 a = kReciprocal[i], difference = a - kReciprocal[i + 1];
    const u32 inverse = a - ((difference >> 4) * f >> 8);  // Q31, 0.5..1
    return shift_saturate(inverse, static_cast<i32>(power) - static_cast<i32>(top) - 31);
}

// tan(pi f) for Q31 f below 0.5, Q28: stmlib's FREQUENCY_FAST polynomial
// f (pi + f^2 (a + b f^2)), clamped below 4.
inline i32 tan_fast(i32 f) {
    const i32 f2 = mul31(f, f);
    const i32 inner = 339169749 + mul31(1871914135, f2);  // Q25: 0.326 pi^3, 0.1823 pi^5
    const i32 sum = 421657428 + mul31(inner, f2) * 4;     // Q27: pi + f^2 inner
    return clamp(mul31(sum, f) * 2, 1, 4 * kOne28 - 1);
}
// stmlib's FREQUENCY_DIRTY tan, f (pi + a f^2), as g / f, Q28. Multiply by f for g.
inline i32 tan_dirty_ratio(i32 f) {
    return 843314857 + mul31(mul31(f, f), 1554770775) * 2;  // Q28: pi + 0.3736 pi^3 f^2 (Q27)
}
// tan(pi f) from the exact table (FREQUENCY_ACCURATE), Q28, clamped below 6.
inline i32 tan_accurate(i32 f) {
    const u32 x = static_cast<u32>(clamp(f, 0, 0x3fffffff));  // f < 0.5
    return clamp(lerp(kTan, x >> 21, (x >> 13) & 255u), 1, 6 * kOne28 - 1);
}

// stmlib SoftLimit on Q26 audio, through the table: x (27 + x^2) / (27 + 9 x^2).
inline i32 soft_limit(i32 x) {
    const i32 magnitude = x < 0 ? -x : x;
    const i32 i = magnitude >> 20;  // 64 steps per unit; the table is flat from 3
    if (i >= 192) return x < 0 ? -kUnit : kUnit;
    const i32 a = kSoftLimit[i];
    const i32 y = (a + mul31(kSoftLimit[i + 1] - a, (magnitude & 0xfffff) << 11)) >> 5;  // Q26
    return x < 0 ? -y : y;
}

// stmlib's DCBlocker, y = x - x[-1] + pole y[-1], with the pole 1 - 2^-shift
// (Rings' 1 - 10/fs is 1 - 2^-12 at 44.1 kHz, 1 - 20/fs is 1 - 2^-11). The
// leak y 2^-shift keeps its remainder for the next sample: rounded down each
// time, it would add a constant offset that a string's loop amplifies.
struct DcBlocker {
    i32 x, y, remainder;
    template <unsigned shift> i32 process(i32 in) {
        const i32 total = y + remainder, leak = mul31(total, 1 << (31 - shift));  // floor(total 2^-shift)
        remainder = total - leak * (1 << shift);
        y += in - x - leak;
        x = in;
        return y;
    }
};

// The two's-complement reading of a word, without implementation-defined conversion.
inline i32 as_signed(u32 word) {
    return word < 0x80000000u ? static_cast<i32>(word) : -static_cast<i32>(~word) - 1;
}

// stmlib's Random: the same linear congruential generator, as 2 GetFloat() - 1
// in Q31.
inline i32 noise_of(u32& seed) {
    seed = seed * 1664525u + 1013904223u;
    return as_signed(seed ^ 0x80000000u);
}

// sin(2 pi t) for a Q32 phase t, Q31: the parabola 8t (1 - 2|t|) over
// -0.5 <= t < 0.5, refined toward a sine (within about 0.1%), for slow modulation.
inline i32 sine(u32 phase) {
    const i32 u = as_signed(phase);                                  // 2t, Q31
    const i32 p = mul31(u, kMaximum - absolute(u)) * 2;              // Q30, -1..1
    const i32 y = p + mul31(mul31(p, absolute(p)) * 2 - p, 483183821);  // + 0.225 (p|p| - p)
    return y >= 0x40000000 ? kMaximum : y <= -0x40000000 ? -kMaximum : y * 2;
}
}  // namespace chimes
