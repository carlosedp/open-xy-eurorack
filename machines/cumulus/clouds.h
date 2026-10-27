#pragma once
#include "core.h"

// Clouds' granular mode in fixed point, block by 32-sample block as Clouds
// runs it: GranularProcessor::Process in PLAYBACK_MODE_GRANULAR at its 16-bit
// stereo quality, all wet. The input takes the feedback, is recorded
// (AudioBuffer::WriteFade), played back as grains (GranularSamplePlayer::Play,
// Grain::OverlapAdd), diffused (Diffuser) and sent through the reverb
// (Reverb). Time constants are Clouds' at 32 kHz, rescaled to 44.1 kHz.
namespace cumulus {
const i32 kPerBlockUp = 1743553487;     // SLOPE(num_grains_, ..., 0.9, 0.2) per 1 ms block, per 0.73 ms: Q31
const i32 kPerBlockDown = 321025712;
const i32 kGainSmoothing = 15604117;    // ONE_POLE(gain_normalization_, ..., 0.01) per sample, Q31
const i32 kHintSmoothing = 214748365;   // ONE_POLE(grain_size_hint_, ..., 0.1) per grain, Q31
const i32 kFreezeSmoothing = 779186;    // ONE_POLE(freeze_lp_, ..., 0.0005) per block, Q31
const i32 kPostGain = 322122547;        // 1.2, Q28
const i32 kAllPass = 1342177280;        // the diffuser's 0.625, Q31
// FxEngine lays its delay lines end to end in one ring and reads a line's
// tail length - 1 samples after writing it. Here each line is a ring of its
// own, length - 1 words, so that its tail is also the word written next:
// read, then overwrite, without a mask. The same delays, exactly.
// The diffuser's eight all-passes, four a channel: Clouds' lengths at 44.1 kHz.
const u32 kDiffuserSize[8] = {173, 247, 370, 611, 207, 282, 337, 557};
const u32 kDiffuserBase[8] = {0, 173, 420, 790, 1401, 1608, 1890, 2227};
// The reverb's four input all-passes, two loop all-passes and a delay each
// side: Clouds' lengths at 44.1 kHz. The left side's delay, which the right
// side reads, keeps a block more (see reverberate).
enum ReverbLine : u32 { kAp1, kAp2, kAp3, kAp4, kDap1a, kDap1b, kDel1, kDap2a, kDap2b, kDel2 };
const u32 kReverbSize[10] = {155, 222, 331, 549, 2277, 2808, 4700 + kBlock, 2635, 2291, 6589};
const u32 kReverbBase[10] = {0, 155, 377, 708, 1257, 3534, 6342, 11074, 13709, 16000};
const u32 kDel1Tail = 4700;  // the left delay's length - 1
const i32 kSmearOffset = 903168, kSmearDepth = 5419008;  // AP1's modulated read, 10 + 60 lfo samples at 32 kHz, Q16
const u32 kSmearWrite = 138;                              // and its write, 100 samples at 32 kHz
const i32 kLoopOffset = 422682624, kLoopDepth = 9031680;  // the long delay's read, 4680 + 100 lfo, Q16
const u32 kLfoIncrement[2] = {1558265, 934959};           // 0.5 and 0.3 Hz, per 32 samples, Q32
const i32 kReverbDiffusion = 1503238554;                  // 0.7, Q31
const i32 kReverbInput = 429496730;                       // 0.2, Q31
const i32 kReverbLimit = 1 << 29;                         // FORMAT_12_BIT's clipping, 8.0, Q26

// A recorded sample: Q26, clipped as Clouds' Clip16(x 32768) clips. Unlike
// Clouds', it keeps 32 bits: an OP-XY send arrives far below Clouds' full
// scale, where 16 bits would leave a few steps of each note.
const i32 kRecordLow = -32768 * 2048, kRecordHigh = 32767 * 2048;
inline i32 to_sample(i32 x) { return clamp(x, kRecordLow, kRecordHigh); }

// lut_window at a Q15 triangle, interpolated. The window rises, so every
// difference is positive and a shift divides it.
inline i32 window_of(i32 triangle) {
    const u32 i = static_cast<u32>(triangle) >> 7, f = static_cast<u32>(triangle) & 127u;
    return kWindow[i] + (((kWindow[i + 1] - kWindow[i]) * static_cast<i32>(f)) >> 7);
}

// Clouds' Pitch notches (lut_quantized_pitch), Q16 semitones, and the values
// of 99 they fall on. Clouds spaces them evenly, a fifteenth of the knob
// apart, which leaves most notches between two values: each falls on the
// nearest whole value here, so that +7 and +12 play in tune.
const i32 kNotches[16] = {-24 * 65536, -12 * 65536, -7 * 65536, -4 * 65536, -3 * 65536, -1 * 65536,
                          -6554,       0,           0,          6554,       1 * 65536,  3 * 65536,
                          4 * 65536,   7 * 65536,   12 * 65536, 24 * 65536};
const u32 kNotchAt[16] = {0, 7, 13, 20, 26, 33, 40, 46, 53, 59, 66, 73, 79, 86, 92, 99};

// A control in values of 99, Q16, within one 65536th: n / 99 reads n.
inline u32 values_of(i32 knob) { return (static_cast<u32>(control(knob)) >> 7) * 99 >> 8; }

// The Pitch knob in semitones, Q16: between notches, Clouds' crossfade, a
// raised cosine with a fifth of a straight line, which lingers at each notch.
inline i32 pitch_of(i32 knob) {
    const u32 steps = values_of(knob);
    u32 i = 0;
    while (i < 14 && steps >= kNotchAt[i + 1] << 16) ++i;
    const u32 span = (kNotchAt[i + 1] - kNotchAt[i]) << 16;
    const i32 fraction = clamp(static_cast<i32>((steps - (kNotchAt[i] << 16)) / (span >> 15)), 0, 32767);  // Q15
    const i32 crossfade = (window_of(fraction) * 4 + fraction) / 5;
    return kNotches[i] + mul31(kNotches[i + 1] - kNotches[i], crossfade << 16);
}

// The controls as Clouds' CvScaler and GranularProcessor::ProcessGranular derive them.
inline Controls controls_of(const i32* parameters) {
    Controls c;
    c.position = control(parameters[kPosition]);
    c.size = control(parameters[kSize]);
    const i32 density = control(parameters[kDensity]), texture = control(parameters[kTexture]);
    // Density is Clouds' meta parameter: grains at regular intervals below its
    // middle, at random above it, more of them further from it.
    c.deterministic = density < 0x40000000;
    c.overlap = density >= 1138166333 ? mul31(density - 1138166333, 1138166333) * 4      // (d - 0.53) 2.12
                : density <= 1009317315 ? mul31(1009317315 - density, 1138166333) * 4    // (0.47 - d) 2.12
                : 0;
    // Texture shapes the window to 0.75, then diffuses.
    c.window_shape = texture < 1610612736 ? clamp(mul31(texture, 1431297851) * 2, 0, kMaximum) : kMaximum;
    c.diffusion = texture > 1610612736 ? clamp((texture - 1610612736) * 4, 0, kMaximum) : 0;
    c.pitch = pitch_of(parameters[kPitch]);
    c.feedback = control(parameters[kFeedback]);
    c.reverb = control(parameters[kReverbAmount]);
    c.freeze = control(parameters[kFreeze]) >= 0x40000000;
    return c;
}

// AudioBuffer::Write: one sample a channel, repeating the first kTail after the end.
inline void write(Clouds& k, i32 l, i32 r) {
    const i32 a = to_sample(l), b = to_sample(r);
    k.left[k.head] = a;
    k.right[k.head] = b;
    if (k.head < kTail) {
        k.left[k.head + kBuffer] = a;
        k.right[k.head + kBuffer] = b;
    }
    if (++k.head >= kBuffer) k.head = 0;
}

// AudioBuffer::WriteFade. Frozen, the input goes on into the splice, which
// crossfades into the recording when it resumes.
inline void record(Clouds& k, const i32* l, const i32* r, bool freeze) {
    if (freeze) {
        for (u32 i = 0; i < kBlock && k.crossfade < kCrossFade; ++i) {
            k.splice[0][k.crossfade] = to_sample(l[i]);
            k.splice[1][k.crossfade++] = to_sample(r[i]);
        }
        return;
    }
    if (!k.crossfade && k.head >= kTail && k.head <= kBuffer - static_cast<i32>(kBlock) - 1) {
        // Clouds' fast write, the most common case: no splice, no wrap, no tail.
        for (u32 i = 0; i < kBlock; ++i) {
            k.left[k.head + i] = to_sample(l[i]);
            k.right[k.head + i] = to_sample(r[i]);
        }
        k.head += kBlock;
        return;
    }
    for (u32 i = 0; i < kBlock; ++i) {
        i32 a = l[i], b = r[i];
        if (k.crossfade) {
            --k.crossfade;
            const i32 fade = static_cast<i32>(k.crossfade << 23);
            a += mul31(k.splice[0][kCrossFade - k.crossfade] - a, fade);
            b += mul31(k.splice[1][kCrossFade - k.crossfade] - b, fade);
        }
        write(k, a, b);
    }
}

// Laurent de Soras's Hermite interpolation as Clouds' ReadHermite reads it:
// between s[1] and s[2], t in Q31. Every term is doubled, which keeps Clouds'
// halves exact; the recording's range keeps every term below 2^31. Q26.
inline i32 hermite(const i32* s, i32 t) {
    const i32 xm1 = s[0], x0 = s[1], x1 = s[2], x2 = s[3];
    const i32 c = x1 - xm1;
    const i32 v = (x0 - x1) * 2;
    const i32 w = c + v;
    const i32 a = w + v + (x2 - x0);
    const i32 b = w + a;
    return (mul31(mul31(mul31(a, t) - b, t) + c, t) + x0 * 2) / 2;  // toward zero: no offset to feed back
}

// Grain::Start.
inline void start_grain(Grain& g, u32 pre_delay, i32 start, i32 width, u32 increment, i32 window_shape,
                        bool high) {
    g.pre_delay = pre_delay;
    while (start < 0) start += kBuffer;
    while (start >= kBuffer) start -= kBuffer;
    g.first = start;
    g.increment = increment;
    g.phase = 0;
    g.envelope = 0;
    g.envelope_increment = 0x80000000u / static_cast<u32>(width);
    if (window_shape >= 1 << 30) {
        g.smoothness = (window_shape - (1 << 30)) >> 15;  // (shape - 0.5) 2, Q15
        g.slope = 0;
    } else {
        g.smoothness = 0;
        g.slope = static_cast<i32>(reciprocal(static_cast<u32>(window_shape + 21474836), 40));  // 0.5 / (shape + 0.01), Q10
    }
    g.high = high;
    g.active = 1;
}

// GranularSamplePlayer::ScheduleGrain.
inline void schedule(Clouds& k, Grain& g, const Controls& c, u32 pre_delay, i32 head, bool high) {
    i32 size = lookup(kGrainSize, c.size);  // Q8 samples
    const u32 ratio = exp2(octaves(c.pitch), 16);  // Q16
    // A grain played faster than it is recorded must not use up the buffer.
    if (ratio > 65536u) size = clamp(size, 0, static_cast<i32>((static_cast<u32>(kBuffer) << 14) / (ratio >> 8)));
    const i32 eaten = static_cast<i32>(multiply_shift(static_cast<u32>(size), ratio, 16));  // by the play head, Q8
    const i32 available = kBuffer * 256 - eaten - size;  // less what the recording head eats
    const i32 reach = mul31(available, c.position) + eaten;  // Q8 samples
    start_grain(g, pre_delay, head - reach / 256, (size >> 8) & ~1, ratio, c.window_shape, high);
    k.size_hint += mul31(size - k.size_hint, kHintSmoothing);
}

// The envelope's gain at a phase, Q15: the high quality, for a smooth window,
// crossfades the triangle into Clouds' raised cosine; without a smooth window
// both qualities clip a steeper triangle into a trapezoid.
template <bool kHigh, bool kSmooth>
inline i32 gain_at(u32 envelope, i32 smoothness, i32 slope) {
    if (envelope >= 0x80000000u) return 0;
    const u32 triangle = envelope >= 0x40000000u ? 0x80000000u - envelope : envelope;  // Q30
    i32 gain = static_cast<i32>(triangle >> 15);  // Q15
    if (kSmooth) {
        if (kHigh) gain += shift_down((window_of(gain) - gain) * smoothness, 15);
    } else {
        gain = (gain * slope) >> 10;
    }
    return gain > 32767 ? 32767 : gain;
}

// Grain::RenderEnvelope: the envelope's gains for the rest of the block, Q31,
// until it ends; returns how many, fewer than size when the grain ends. The
// gain is exact every kStep samples and interpolated between: Clouds' shortest
// grain spans 1411 samples, so the window, even a trapezoid's corner, departs
// from its line by less than 1e-4 within a step.
const u32 kStep = 8;
template <bool kHigh, bool kSmooth>
inline u32 render_envelope(Grain& g, i32* gains, u32 size) {
    const u32 increment = g.envelope_increment;
    // Clouds stops the grain at the first sample whose next phase reaches 2.
    const u32 left = (0x80000000u - g.envelope + increment - 1) / increment - 1;
    const u32 n = left < size ? left : size;
    if (left < size) g.active = 0;
    u32 envelope = g.envelope;
    i32 previous = gain_at<kHigh, kSmooth>(envelope, g.smoothness, g.slope);
    for (u32 t = 0; t < n; t += kStep) {
        const u32 m = n - t < kStep ? n - t : kStep;
        envelope += m * increment;
        const i32 next = gain_at<kHigh, kSmooth>(envelope, g.smoothness, g.slope);
        const i32 difference = next - previous;
        if (m == kStep) {
            for (u32 j = 0; j < kStep; ++j) gains[t + j] = (previous + shift_down(difference * static_cast<i32>(j), 3)) << 16;
        } else {
            for (u32 j = 0; j < m; ++j) gains[t + j] = (previous + difference * static_cast<i32>(j) / static_cast<i32>(m)) << 16;
        }
        previous = next;
    }
    g.envelope = envelope;
    return n;
}

// Grain::OverlapAdd<2> for one block, at the centre of the stereo field
// (Clouds' Spread at 0), so each channel plays its own recording: the high
// quality reads Hermite, the medium quality linearly. A grain at the
// recording's own pitch (kExact) reads whole samples, where both
// interpolations return a sample as it is: Hermite the one after the read
// position, linear the one at it.
template <bool kHigh, bool kSmooth, bool kExact>
inline void overlap_add(Grain& g, const Clouds& k, i32* out_l, i32* out_r) {
    const u32 start = g.pre_delay < kBlock ? g.pre_delay : kBlock;
    g.pre_delay -= start;
    i32 gains[kBlock];
    const u32 n = render_envelope<kHigh, kSmooth>(g, gains, kBlock - start);
    out_l += start;
    out_r += start;
    u32 phase = g.phase;
    const u32 increment = g.increment;
    const i32 first = g.first;
    for (u32 t = 0; t < n; ++t) {
        i32 index = first + static_cast<i32>(phase >> 16);
        if (index >= kBuffer) index -= kBuffer;
        const i32 fraction = static_cast<i32>((phase & 0xffffu) << 15);
        i32 l, r;  // Q26
        if (kExact) {
            l = k.left[kHigh ? index + 1 : index];
            r = k.right[kHigh ? index + 1 : index];
        } else if (kHigh) {
            l = hermite(k.left + index, fraction);
            r = hermite(k.right + index, fraction);
        } else {
            const i32 l0 = k.left[index], r0 = k.right[index];
            l = l0 + mul31(k.left[index + 1] - l0, fraction);
            r = r0 + mul31(k.right[index + 1] - r0, fraction);
        }
        out_l[t] += mul31(l, gains[t]);
        out_r[t] += mul31(r, gains[t]);
        phase += increment;
    }
    g.phase = phase;
}

template <bool kHigh, bool kSmooth>
inline void overlap_add(Grain& g, const Clouds& k, i32* out_l, i32* out_r) {
    if (g.increment == 65536u) overlap_add<kHigh, kSmooth, true>(g, k, out_l, out_r);
    else overlap_add<kHigh, kSmooth, false>(g, k, out_l, out_r);
}

// GranularSamplePlayer::Play for one block: schedule, overlap, normalize.
inline void play(Clouds& k, const Controls& c, i32* out_l, i32* out_r) {
    // Clouds aims for 32 overlap^3 grains, seeded at random with a probability
    // per sample, or at regular intervals, from the recent grain size.
    const i32 overlap3 = mul31(mul31(c.overlap, c.overlap), c.overlap);
    const i32 target = overlap3 >> 10;  // Q16 grains
    u32 probability = 0;                // Q32 per sample
    i32 space = 0x7fffffff;             // samples between grains, Q8
    if (target >> 8) space = static_cast<i32>((static_cast<u32>(k.size_hint) << 8) / static_cast<u32>(target >> 8));
    if (!c.deterministic) {
        k.phasor = -1000 * 256;
        if (target) probability = multiply_shift(static_cast<u32>(target), reciprocal(static_cast<u32>(k.size_hint), 50), 26);
    }

    u32 available[kGrains], count = 0;
    for (u32 i = 0; i < kGrains; ++i)
        if (!k.grains[i].active) available[count++] = i;
    for (u32 t = 0; t < kBlock; ++t) {
        k.phasor += 256;
        const bool random = random_word(k.seed) < probability && target > k.num_grains;
        if (count && (random || k.phasor >= space)) {
            --count;
            schedule(k, k.grains[available[count]], c, t, k.head - static_cast<i32>(kBlock) + static_cast<i32>(t),
                     count >= kGrains - (kHighGrains < kGrains ? kHighGrains : kGrains));
            k.phasor = 0;
        }
    }

    for (u32 t = 0; t < kBlock; ++t) out_l[t] = out_r[t] = 0;
    for (u32 i = 0; i < kGrains; ++i) {
        Grain& g = k.grains[i];
        if (!g.active) continue;
        if (g.high) {
            if (g.smoothness) overlap_add<true, true>(g, k, out_l, out_r);
            else overlap_add<true, false>(g, k, out_l, out_r);
        } else {
            if (g.smoothness) overlap_add<false, true>(g, k, out_l, out_r);
            else overlap_add<false, false>(g, k, out_l, out_r);
        }
    }

    // Normalize by the number of grains playing, 1 / sqrt(n - 1), raised for
    // dense, smooth windows: Crossfade(1, 1 + 2 shape, overlap).
    const i32 playing = static_cast<i32>(kGrains - count) << 16;
    k.num_grains += mul31(playing - k.num_grains, playing > k.num_grains ? kPerBlockUp : kPerBlockDown);
    i32 gain = kOne28;
    if (k.num_grains > 2 << 16) {
        const u32 root = square_root(static_cast<u32>(k.num_grains - (1 << 16)) << 10);  // Q13
        gain = static_cast<i32>(reciprocal(root, 13 + 28));
    }
    const i32 window_gain = clamp(kOne28 + (c.window_shape >> 2), kOne28, 2 * kOne28);  // 1 + 2 shape, Q28
    gain = mul28(gain, kOne28 + mul31(window_gain - kOne28, c.overlap));
    i32 smoothed = k.gain;
    for (u32 t = 0; t < kBlock; ++t) {
        smoothed += mul31(gain - smoothed, kGainSmoothing);
        out_l[t] = mul28(out_l[t], smoothed);
        out_r[t] = mul28(out_r[t], smoothed);
    }
    k.gain = smoothed;
}

// stmlib Svf high-pass at f (Q31 cycles per sample, below about 0.01), Q of 1:
// g = tan(pi f), which pi f gives within 1e-4 here.
inline void highpass(Svf& svf, i32* x, i32 f) {
    const i32 g = mul31(f, 1686629713) * 4;                              // pi f, Q31
    const i32 h = static_cast<i32>(reciprocal(static_cast<u32>((1 << 30) + (g >> 1) + (mul31(g, g) >> 1)), 61));  // 1 / (1 + g + g^2), Q31
    i32 s1 = svf.s1, s2 = svf.s2;
    for (u32 t = 0; t < kBlock; ++t) {
        const i32 hp = mul31(x[t] - s1 - mul31(s1, g) - s2, h);
        const i32 bp = mul31(hp, g) + s1;
        s1 = mul31(hp, g) + bp;
        const i32 lp = mul31(bp, g) + s2;
        s2 = mul31(bp, g) + lp;
        x[t] = hp;
    }
    svf.s1 = s1;
    svf.s2 = s2;
}

// One delay line: a ring of words and its position, which FxEngine steps
// back a sample at the start of each sample.
struct Ring {
    i32* words;
    u32 size, position;
    void step() { position = position ? position - 1 : size - 1; }
    // The tail, length - 1 samples old, which is also the word written next.
    i32& tail() const { return words[position]; }
    // The word `offset` samples old.
    i32& at(u32 offset) const {
        const u32 i = position + offset;
        return words[i < size ? i : i - size];
    }
    // A read between samples, offset Q16 samples old.
    i32 interpolate(i32 offset) const {
        const u32 whole = static_cast<u32>(offset >> 16);
        const i32 a = at(whole), b = at(whole + 1);
        return a + mul31(b - a, (offset & 0xffff) << 15);
    }
    // Context::Read of the tail, then WriteAllPass.
    i32 all_pass(i32 accumulator, i32 coefficient) const {
        i32& word = tail();
        const i32 delayed = word;
        accumulator += mul31(delayed, coefficient);
        word = accumulator;
        return mul31(accumulator, -coefficient) + delayed;
    }
    // The same in the reverb, whose 12-bit storage (FORMAT_12_BIT) clips at 8.
    i32 all_pass_12(i32 accumulator, i32 coefficient) const {
        i32& word = tail();
        const i32 delayed = word;
        accumulator += mul31(delayed, coefficient);
        word = clamp(accumulator, -kReverbLimit, kReverbLimit);
        return mul31(accumulator, -coefficient) + delayed;
    }
};
inline Ring diffuser_line(Clouds& k, u32 i) {
    const Ring ring = {k.diffusion + kDiffuserBase[i], kDiffuserSize[i], k.diffusion_position[i]};
    return ring;
}
inline Ring reverb_line(Clouds& k, u32 i) {
    const Ring ring = {k.reverb + kReverbBase[i], kReverbSize[i], k.reverb_position[i]};
    return ring;
}

// Diffuser, four all-passes a channel, mixed in by Texture's top quarter. The
// channels share no line, so each runs as a pass over the block, which
// leaves the Blackfin's registers enough for one.
inline void diffuse_channel(Clouds& k, i32* out, i32 amount, u32 first) {
    Ring a = diffuser_line(k, first), b = diffuser_line(k, first + 1);
    Ring c = diffuser_line(k, first + 2), d = diffuser_line(k, first + 3);
    for (u32 t = 0; t < kBlock; ++t) {
        a.step();
        b.step();
        c.step();
        d.step();
        const i32 dry = out[t];
        i32 accumulator = a.all_pass(dry, kAllPass);
        accumulator = b.all_pass(accumulator, kAllPass);
        accumulator = c.all_pass(accumulator, kAllPass);
        accumulator = d.all_pass(accumulator, kAllPass);
        out[t] = dry + mul31(accumulator - dry, amount);
    }
    k.diffusion_position[first] = a.position;
    k.diffusion_position[first + 1] = b.position;
    k.diffusion_position[first + 2] = c.position;
    k.diffusion_position[first + 3] = d.position;
}
inline void diffuse(Clouds& k, i32* out_l, i32* out_r, i32 amount) {
    if (!amount) return;
    diffuse_channel(k, out_l, amount, 0);
    diffuse_channel(k, out_r, amount, 4);
}

// Reverb: Griesinger's topology after Dattorro, as Clouds runs it: four
// all-passes diffuse the input, then a loop of two all-passes and a delay each
// side, with the first all-pass and the long delay slowly modulated. Clouds
// steps its modulation every 32 samples; here at each block's start.
//
// The input diffusion and the two sides of the loop run as passes over the
// block, each with registers enough: within a block no side reads what the
// other writes. The right side reads the left's delay 4700 samples back,
// written before this block, and the left delay's ring keeps a block more than
// that, so the left pass, which runs first, does not overwrite it. The left
// side reads the right's delay at least 6449 samples back, before the right
// pass writes this block.
inline void reverberate(Clouds& k, i32* out_l, i32* out_r, i32 amount, i32 time, i32 damping) {
    if (!amount) return;
    for (u32 i = 0; i < 2; ++i) {
        k.lfo[i] += kLfoIncrement[i];
        k.lfo_value[i] = (kMaximum >> 1) + (sine(k.lfo[i] + 0x40000000u) >> 1);
    }
    const i32 smear_offset = kSmearOffset + mul31(kSmearDepth, k.lfo_value[0]);
    const i32 loop_offset = kLoopOffset + mul31(kLoopDepth, k.lfo_value[1]);
    const i32 kap = kReverbDiffusion;
    i32 apout[kBlock];
    {
        Ring ap1 = reverb_line(k, kAp1), ap2 = reverb_line(k, kAp2);
        Ring ap3 = reverb_line(k, kAp3), ap4 = reverb_line(k, kAp4);
        for (u32 t = 0; t < kBlock; ++t) {
            ap1.step();
            ap2.step();
            ap3.step();
            ap4.step();
            // Smear AP1 inside the loop.
            ap1.at(kSmearWrite) = clamp(ap1.interpolate(smear_offset), -kReverbLimit, kReverbLimit);
            i32 accumulator = ap1.all_pass_12(mul31(out_l[t] + out_r[t], kReverbInput), kap);
            accumulator = ap2.all_pass_12(accumulator, kap);
            accumulator = ap3.all_pass_12(accumulator, kap);
            apout[t] = ap4.all_pass_12(accumulator, kap);
        }
        k.reverb_position[kAp1] = ap1.position;
        k.reverb_position[kAp2] = ap2.position;
        k.reverb_position[kAp3] = ap3.position;
        k.reverb_position[kAp4] = ap4.position;
    }
    {
        Ring del2 = reverb_line(k, kDel2), dap1a = reverb_line(k, kDap1a);
        Ring dap1b = reverb_line(k, kDap1b), del1 = reverb_line(k, kDel1);
        i32 lp = k.damping[0];
        for (u32 t = 0; t < kBlock; ++t) {
            del2.step();
            dap1a.step();
            dap1b.step();
            del1.step();
            lp += mul31(apout[t] + mul31(del2.interpolate(loop_offset), time) - lp, damping);
            i32 accumulator = dap1a.all_pass_12(lp, -kap);
            accumulator = dap1b.all_pass_12(accumulator, kap);
            del1.at(0) = clamp(accumulator, -kReverbLimit, kReverbLimit);
            out_l[t] += mul31(accumulator * 2 - out_l[t], amount);
        }
        k.damping[0] = lp;
        k.reverb_position[kDap1a] = dap1a.position;
        k.reverb_position[kDap1b] = dap1b.position;
    }
    {
        Ring del1 = reverb_line(k, kDel1), dap2a = reverb_line(k, kDap2a);
        Ring dap2b = reverb_line(k, kDap2b), del2 = reverb_line(k, kDel2);
        i32 lp = k.damping[1];
        for (u32 t = 0; t < kBlock; ++t) {
            del1.step();
            dap2a.step();
            dap2b.step();
            del2.step();
            lp += mul31(apout[t] + mul31(del1.at(kDel1Tail), time) - lp, damping);
            i32 accumulator = dap2a.all_pass_12(lp, kap);
            accumulator = dap2b.all_pass_12(accumulator, -kap);
            del2.tail() = clamp(accumulator, -kReverbLimit, kReverbLimit);
            out_r[t] += mul31(accumulator * 2 - out_r[t], amount);
        }
        k.damping[1] = lp;
        k.reverb_position[kDap2a] = dap2a.position;
        k.reverb_position[kDap2b] = dap2b.position;
        k.reverb_position[kDel1] = del1.position;
        k.reverb_position[kDel2] = del2.position;
    }
}

// One 32-sample block of Clouds, all wet: in and out are Q26 within Clouds'
// full scale, in place.
inline void process_block(Clouds& k, const Controls& c, i32* l, i32* r) {
    // Feedback of the last block's output, high-passed so that it does not
    // build up low frequencies, and soft-limited into the input. Freeze hands
    // it over to the reverb.
    // Without Feedback none of it is heard, so the filters wait.
    k.freeze_lp += mul31((c.freeze ? kMaximum : 0) - k.freeze_lp, kFreezeSmoothing);
    if (c.feedback) {
        const i32 f2 = mul31(c.feedback, c.feedback);
        const i32 cutoff = 973915 + mul31(f2, 4869577);  // (20 + 100 feedback^2) / 44100, Q31
        highpass(k.highpass[0], k.feedback[0], cutoff);
        highpass(k.highpass[1], k.feedback[1], cutoff);
        const i32 fb_gain = mul31(c.feedback, kMaximum - k.freeze_lp);
        const i32 fb_drive = mul31(fb_gain, 1503238554);  // 1.4 fb_gain, Q30
        for (u32 t = 0; t < kBlock; ++t) {
            l[t] += mul31(soft_limit(mul31(k.feedback[0][t], fb_drive) * 2 + l[t]) - l[t], fb_gain);
            r[t] += mul31(soft_limit(mul31(k.feedback[1][t], fb_drive) * 2 + r[t]) - r[t], fb_gain);
        }
    }

    record(k, l, r, c.freeze != 0);
    i32 out_l[kBlock], out_r[kBlock];
    play(k, c, out_l, out_r);
    diffuse(k, out_l, out_r, c.diffusion);
    for (u32 t = 0; t < kBlock; ++t) {
        k.feedback[0][t] = out_l[t];
        k.feedback[1][t] = out_r[t];
    }

    // The reverb, its amount raised by the feedback while frozen.
    const i32 held = c.feedback + mul31(c.feedback, kMaximum - c.feedback);  // feedback (2 - feedback)
    const i32 amount = clamp(mul31(c.reverb, 2040109466) + mul31(held, k.freeze_lp), 0, kMaximum);  // 0.95 reverb + ...
    reverberate(k, out_l, out_r, mul31(amount, 1159641170),                  // 0.54 amount
                751619277 + mul31(amount, 1352914698),                       // time 0.35 + 0.63 amount
                lookup(kReverbDamping, c.feedback));                         // 0.6 + 0.37 feedback

    // All wet: Clouds' crossfade at its end, the wet side 1.2 times louder,
    // then SoftConvert, which limits half the sum.
    const i32 wet = mul28(kFadeIn[16], kPostGain) / 2;
    for (u32 t = 0; t < kBlock; ++t) {
        l[t] = soft_limit(mul31(out_l[t], wet));
        r[t] = soft_limit(mul31(out_r[t], wet));
    }
}
}  // namespace cumulus
