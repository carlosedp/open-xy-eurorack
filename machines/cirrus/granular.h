#pragma once
#include "core.h"

// Clouds' granular player in fixed point, block by 32-sample block as Clouds
// runs it: GranularSamplePlayer::Play and Grain::OverlapAdd at the 16-bit
// quality, all wet, reading the note's sample where Clouds reads its
// recording. Time constants are Clouds' at 32 kHz, rescaled to 44.1 kHz.
namespace cirrus {
const i32 kPerBlockUp = 1743553487;     // SLOPE(num_grains_, ..., 0.9, 0.2) per 1 ms block, per 0.73 ms: Q31
const i32 kPerBlockDown = 321025712;
const i32 kGainSmoothing = 15604117;    // ONE_POLE(gain_normalization_, ..., 0.01) per sample, Q31
const i32 kHintSmoothing = 214748365;   // ONE_POLE(grain_size_hint_, ..., 0.1) per grain, Q31
// All wet: Clouds' crossfade at its end, the wet side 1.2 times louder, and
// half of it, which SoftConvert limits. Folded into the grains' normalization.
const i32 kWet = 911100148;             // lut_xfade_in's end (0.707) 1.2 / 2, Q31
const u32 kMiddleC = 11046847;          // 2^48 / middle C's phase increment at 44.1 kHz
const u32 kMaxRatio = 32u << 16;        // the fastest a grain reads its sample: five octaves up

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

// The Speed knob as a fraction of the sample's own speed, Q16: still at 50,
// forward at its own speed at 99, backward at 1.
inline i32 speed_of(i32 knob) {
    return clamp((static_cast<i32>(values_of(knob)) - 50 * 65536) / 49, -65536, 65536);
}

// The controls as Clouds' CvScaler and GranularProcessor::ProcessGranular derive them.
inline Controls controls_of(opxy::Parameters parameters) {
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
    // Texture shapes the window up to 0.75, where Clouds' diffuser takes over.
    c.window_shape = texture < 1610612736 ? clamp(mul31(texture, 1431297851) * 2, 0, kMaximum) : kMaximum;
    c.semitones = pitch_of(parameters[kPitch]);
    c.speed = speed_of(parameters[kSpeed]);
    c.spread = control(parameters[kSpread]);
    return c;
}

// The rate a grain reads the sample, Q16: the note against middle C, where
// stock plays a sample as it is, then the semitones and the sample's rate.
inline u32 ratio_of(u32 increment, i32 semitones, u32 rate) {
    const u32 note = multiply_shift(increment, kMiddleC, 32);           // Q16
    const u32 tuning = exp2(octaves(semitones), 16);                    // Q16
    const u32 resampling = rate * 16384u / 11025u;                      // rate / 44100, Q16
    const u32 ratio = multiply_shift(multiply_shift(note, tuning, 16), resampling, 16);
    return ratio < 1 ? 1 : ratio > kMaxRatio ? kMaxRatio : ratio;
}

// Laurent de Soras's Hermite interpolation as Clouds' ReadHermite reads it:
// between s[1] and s[2], frames kStride words apart, t in Q31. Q16 (twice
// Q15): every term is doubled.
template <u32 kStride>
inline i32 hermite(const i16* s, i32 t) {
    const i32 xm1 = s[0], x0 = s[kStride], x1 = s[2 * kStride], x2 = s[3 * kStride];
    const i32 c = x1 - xm1;
    const i32 v = (x0 - x1) * 2;
    const i32 w = c + v;
    const i32 a = w + v + (x2 - x0);
    const i32 b = w + a;
    return mul31(mul31(mul31(a, t) - b, t) + c, t) + x0 * 2;
}

// Grain::Start.
inline void start_grain(Grain& g, u32 pre_delay, u32 first, i32 width, u32 increment, i32 window_shape,
                        bool high, i32 gain_l, i32 gain_r) {
    g.pre_delay = pre_delay;
    g.first = first;
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
    g.gain_l = gain_l;
    g.gain_r = gain_r;
    g.high = high;
    g.active = 1;
}

// GranularSamplePlayer::ScheduleGrain, in a region of the sample (frames long,
// at least kShortest). Clouds keeps a grain inside its recording, shortening
// it when it reads faster; here inside the region. Position places it in the
// room the grain leaves, and the scan moves it on from there.
inline void schedule(Voice& v, Grain& g, const Controls& c, u32 pre_delay, u32 ratio, u32 begin, u32 frames,
                     bool high) {
    i32 size = lookup(kGrainSize, c.size);  // Q8 samples
    const u32 room = frames - kMargin;
    const u32 longest = multiply_shift(room, reciprocal(ratio, 40), 16);  // room 2^24 / ratio: Q8 samples
    if (static_cast<u32>(size) > longest) size = static_cast<i32>(longest);
    if (size < 512) size = 512;  // two samples; the region holds them at any ratio
    const u32 eaten = multiply_shift(static_cast<u32>(size), ratio, 16);  // by the play head, Q8 frames
    const u32 available = eaten + 256 < room << 8 ? (room << 8) - eaten : 256;
    // The scan moves the grains at its speed, wrapping within the room. A grain
    // starts where the scan is at its own sample, as Clouds' start where the
    // recording head is: grains a few samples apart that read the same stretch
    // otherwise sum out of step, a comb that dulls the highs.
    const u32 placed = multiply_shift(available, static_cast<u32>(c.position) << 1, 32);  // Q8 frames
    const u32 scan = v.scan + static_cast<u32>(as_signed(v.step) / static_cast<i32>(kBlock) * static_cast<i32>(pre_delay));
    const u32 scanned = multiply_shift(frames << 8, scan, 32);
    const u32 first = begin + ((placed + scanned) % available >> 8);
    // Clouds' pan: random about the centre by Spread. A mono sample pans with
    // equal power, as Clouds' mono mode; a stereo one turns down the far side
    // and crosses its signal to the near one.
    const i32 pan = (1 << 30) + mul31(c.spread, noise_of(v.seed)) / 2;  // Q31, 0..1
    i32 gain_l, gain_r;
    if (v.sample.channels == 1) {
        gain_l = sine(static_cast<u32>(pan) >> 1);
        gain_r = sine((static_cast<u32>(pan) >> 1) + 0x40000000u);
    } else if (pan < 1 << 30) {
        gain_l = kMaximum;
        gain_r = pan * 2;
    } else {
        gain_r = kMaximum;
        gain_l = (kMaximum - pan) * 2;
    }
    start_grain(g, pre_delay, first, (size >> 8) & ~1, ratio, c.window_shape, high, gain_l, gain_r);
    v.size_hint += mul31(size - v.size_hint, kHintSmoothing);
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

// A sample's frame read between frames: the high quality Hermite, the medium
// linearly, Q26.
template <bool kHigh, u32 kStride>
inline i32 read(const i16* s, i32 fraction) {
    if (kHigh) return hermite<kStride>(s, fraction) * (1 << 10);
    const i32 a = s[0];
    return (a + mul31(s[kStride] - a, fraction)) * (1 << 11);
}

// Grain::OverlapAdd<1> for a mono sample: the grain into both sides, by its pan.
template <bool kHigh, bool kSmooth>
inline void overlap_add_mono(Grain& g, const i16* data, i32* out_l, i32* out_r) {
    const u32 start = g.pre_delay < kBlock ? g.pre_delay : kBlock;
    g.pre_delay -= start;
    i32 gains[kBlock];
    const u32 n = render_envelope<kHigh, kSmooth>(g, gains, kBlock - start);
    out_l += start;
    out_r += start;
    const i16* s = data + g.first;
    u32 phase = g.phase;
    const u32 increment = g.increment;
    const i32 gain_l = g.gain_l, gain_r = g.gain_r;
    for (u32 t = 0; t < n; ++t) {
        const i32 x = mul31(read<kHigh, 1>(s + (phase >> 16), static_cast<i32>((phase & 0xffffu) << 15)), gains[t]);
        out_l[t] += mul31(x, gain_l);
        out_r[t] += mul31(x, gain_r);
        phase += increment;
    }
    g.first += phase >> 16;
    g.phase = phase & 0xffffu;
}

// Grain::OverlapAdd<2> for a stereo sample. One side keeps its signal (Clouds'
// gain 1); the other turns down by its gain and crosses the rest to the first.
template <bool kHigh, bool kSmooth>
inline void overlap_add_stereo(Grain& g, const i16* data, i32* out_l, i32* out_r) {
    const u32 start = g.pre_delay < kBlock ? g.pre_delay : kBlock;
    g.pre_delay -= start;
    i32 gains[kBlock];
    const u32 n = render_envelope<kHigh, kSmooth>(g, gains, kBlock - start);
    const bool left_near = g.gain_l == kMaximum;
    i32* near = (left_near ? out_l : out_r) + start;
    i32* far = (left_near ? out_r : out_l) + start;
    const i16* s = data + g.first * 2 + (left_near ? 0 : 1);  // the near side's frames; the far side's follow each
    const i32 kept = left_near ? g.gain_r : g.gain_l, crossed = kMaximum - kept;
    u32 phase = g.phase;
    const u32 increment = g.increment;
    for (u32 t = 0; t < n; ++t) {
        const i16* frame = s + (phase >> 16) * 2;
        const i32 fraction = static_cast<i32>((phase & 0xffffu) << 15);
        const i32 a = mul31(read<kHigh, 2>(frame, fraction), gains[t]);
        const i32 b = mul31(read<kHigh, 2>(frame + (left_near ? 1 : -1), fraction), gains[t]);
        near[t] += a + mul31(b, crossed);
        far[t] += mul31(b, kept);
        phase += increment;
    }
    g.first += phase >> 16;
    g.phase = phase & 0xffffu;
}

template <bool kHigh, bool kSmooth>
inline void overlap_add(Grain& g, const opxy::SampleView& sample, i32* out_l, i32* out_r) {
    if (sample.channels == 2) overlap_add_stereo<kHigh, kSmooth>(g, sample.data, out_l, out_r);
    else overlap_add_mono<kHigh, kSmooth>(g, sample.data, out_l, out_r);
}

// GranularSamplePlayer::Play for one block: schedule, overlap, normalize. A
// region shorter than kShortest starts no grain.
inline void play(Voice& v, const Controls& c, u32 ratio, u32 begin, u32 frames, i32* out_l, i32* out_r) {
    // Clouds aims for 32 overlap^3 grains in stereo, 40 in mono, seeded at
    // random with a probability per sample, or at regular intervals, from the
    // recent grain size.
    const i32 overlap3 = mul31(mul31(c.overlap, c.overlap), c.overlap);
    const i32 most = static_cast<i32>(v.sample.channels == 1 ? kMaxMonoGrains : kMaxGrains);
    const i32 target = mul31(overlap3, most << 16);  // Q16 grains
    u32 probability = 0;                // Q32 per sample
    i32 space = 0x7fffffff;             // samples between grains, Q8
    if (target >> 8) space = static_cast<i32>((static_cast<u32>(v.size_hint) << 8) / static_cast<u32>(target >> 8));
    if (!c.deterministic) {
        v.phasor = -1000 * 256;
        if (target) probability = multiply_shift(static_cast<u32>(target), reciprocal(static_cast<u32>(v.size_hint), 50), 26);
    }

    u32 available[kGrains], count = 0;
    for (u32 i = 0; i < kGrains; ++i)
        if (!v.grains[i].active) available[count++] = i;
    if (frames < kShortest) count = 0;
    // A note seeds its first grain at once, as Clouds' trigger input does.
    bool trigger = v.trigger != 0;
    v.trigger = 0;
    for (u32 t = 0; t < kBlock; ++t) {
        v.phasor += 256;
        const bool random = random_word(v.seed) < probability && target > v.num_grains;
        if (count && (random || v.phasor >= space || trigger)) {
            --count;
            schedule(v, v.grains[available[count]], c, t, ratio, begin, frames,
                     count >= kGrains - (kHighGrains < kGrains ? kHighGrains : kGrains));
            v.phasor = 0;
            trigger = false;
        }
    }

    for (u32 t = 0; t < kBlock; ++t) out_l[t] = out_r[t] = 0;
    for (u32 i = 0; i < kGrains; ++i) {
        Grain& g = v.grains[i];
        if (!g.active) continue;
        if (g.high) {
            if (g.smoothness) overlap_add<true, true>(g, v.sample, out_l, out_r);
            else overlap_add<true, false>(g, v.sample, out_l, out_r);
        } else {
            if (g.smoothness) overlap_add<false, true>(g, v.sample, out_l, out_r);
            else overlap_add<false, false>(g, v.sample, out_l, out_r);
        }
    }

    // Normalize by the number of grains playing, 1 / sqrt(n - 1), raised for
    // dense, smooth windows: Crossfade(1, 1 + 2 shape, overlap). Then the wet gain.
    const i32 playing = static_cast<i32>(kGrains - count) << 16;
    v.num_grains += mul31(playing - v.num_grains, playing > v.num_grains ? kPerBlockUp : kPerBlockDown);
    i32 gain = kOne28;
    if (v.num_grains > 2 << 16) {
        const u32 root = square_root(static_cast<u32>(v.num_grains - (1 << 16)) << 10);  // Q13
        gain = static_cast<i32>(reciprocal(root, 13 + 28));
    }
    const i32 window_gain = clamp(kOne28 + (c.window_shape >> 2), kOne28, 2 * kOne28);  // 1 + 2 shape, Q28
    gain = mul31(mul28(gain, kOne28 + mul31(window_gain - kOne28, c.overlap)), kWet);
    i32 smoothed = v.gain;
    for (u32 t = 0; t < kBlock; ++t) {
        smoothed += mul31(gain - smoothed, kGainSmoothing);
        out_l[t] = mul28(out_l[t], smoothed);
        out_r[t] = mul28(out_r[t], smoothed);
    }
    v.gain = smoothed;
}

// The scan for one block: Speed moves where the grains start through the
// region, at the sample's own speed at most, Q32 of the region per block.
inline u32 scan_step(i32 speed, u32 rate, u32 frames) {
    if (!speed || frames < kShortest) return 0;
    const u32 resampling = rate * 16384u / 11025u;  // rate / 44100, Q16
    const u32 magnitude = static_cast<u32>(speed < 0 ? -speed : speed);
    const u32 per_block = multiply_shift(magnitude, resampling, 16) * kBlock;  // Q16 frames
    const unsigned power = 31 + top_bit(frames);
    const u32 step = multiply_shift(per_block, reciprocal(frames, power), power - 16);  // per_block 2^16 / frames
    return speed < 0 ? 0u - step : step;
}
}  // namespace cirrus
