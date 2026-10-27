// Cirrus: Clouds' grains from a sample, played from the keyboard. Each note's
// voice plays the sample in grains at the note's pitch, starting from Position
// and moving through it at Speed.
#include <opxy/sample_engine.hpp>
#include "granular.h"

namespace cirrus {
namespace {
// Each voice's random sequence starts where the last one's started plus one
// step, so that the notes of a chord do not scatter their grains alike.
u32 seeds;

const u32 kLevelLimit = (1u << 30) - 1;  // a pad's level below 8, so that Q28 holds it
}

void initialize(Voice& v, const opxy::SampleView& sample) {
    v = Voice();
    v.sample = sample;
    v.seed = random_word(seeds);
    v.gain = mul31(kOne28, kWet);
    v.size_hint = 361267;  // Clouds' starting grain_size_hint_, 1024 samples at 32 kHz: Q8 at 44.1 kHz
}

// A note plays from Position again and seeds a grain at once.
void start(Voice& v) {
    v.scan = 0;
    v.trigger = 1;
}

void render_sample(Voice& v, const opxy::SampleBlock& block) {
    const u32 channels = block.output_channels == 2 ? 2 : 1;
    const opxy::SampleView& sample = v.sample;
    const u32 length = sample.frames < kFrames ? sample.frames : kFrames;
    const u32 begin = block.region.begin < length ? block.region.begin : length;
    const u32 end = block.region.end < length ? block.region.end : length;
    const u32 frames = end > begin ? end - begin : 0;
    if (!sample.data || (sample.channels != 1 && sample.channels != 2) || frames < kShortest) {
        for (u32 i = 0; i < opxy::block_frames * channels; ++i) block.output[i] = 0;
        return;
    }

    Controls c = controls_of(block.parameters);
    c.semitones += clamp(block.region.pitch_cents, -9600, 9600) * 65536 / 100;
    const u32 rate = sample.rate >= 8000 && sample.rate <= 192000 ? sample.rate : 44100;
    v.step = scan_step(c.speed, rate, frames);
    // SoftConvert's limit, then the pad's level.
    const bool unity = block.region.level == opxy::sample_unity_level;
    const i32 level = static_cast<i32>(block.region.level < kLevelLimit ? block.region.level : kLevelLimit) * 2;  // Q28

    for (u32 half = 0; half < opxy::block_frames / kBlock; ++half) {
        i32 l[kBlock], r[kBlock];
        play(v, c, ratio_of(block.pitch[half * kBlock], c.semitones, rate), begin, frames, l, r);
        v.scan += v.step;
        i32* out = block.output + half * kBlock * channels;
        for (u32 t = 0; t < kBlock; ++t) {
            l[t] = soft_limit(l[t]);
            r[t] = soft_limit(r[t]);
        }
        if (!unity) {
            for (u32 t = 0; t < kBlock; ++t) {
                l[t] = clamp(mul28(l[t], level), -kUnit * 2 + 1, kUnit * 2 - 1);
                r[t] = clamp(mul28(r[t], level), -kUnit * 2 + 1, kUnit * 2 - 1);
            }
        }
        // Q26 at Clouds' full scale to half the OP-XY's.
        if (channels == 2) {
            for (u32 t = 0; t < kBlock; ++t) {
                out[t * 2] = l[t] * 16;
                out[t * 2 + 1] = r[t] * 16;
            }
        } else {
            for (u32 t = 0; t < kBlock; ++t) out[t] = (l[t] + r[t]) * 8;
        }
    }
}
}  // namespace cirrus
