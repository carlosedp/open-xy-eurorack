// Upstream Clouds (float, 32 kHz) in granular mode, by default at its 16-bit
// stereo quality and without stereo spread, all wet, the input on both
// channels: its output, interleaved left and right, as 32-bit floats in
// Clouds' units.
//   reference <position> <size> <density> <texture> <pitch> <feedback> <reverb> <freeze at> <seconds> <in.f32> <out.f32> [seed [spread [quality]]]
// Controls are the knobs, 0..1, Pitch through Clouds' notched table as its
// CvScaler reads it. Freeze turns on at <freeze at> seconds (never when
// negative). <in.f32> holds the input at 32 kHz. Quality is Clouds' (0
// 16-bit stereo, 1 16-bit mono).
#include "clouds/dsp/granular_processor.h"
#include "clouds/resources.h"
#include "stmlib/utils/random.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace clouds;
static uint8_t large_buffer[118784];
static uint8_t small_buffer[65536 - 128];
static GranularProcessor processor;

int main(int argc, char** argv) {
    if (argc < 12) {
        std::fprintf(stderr, "usage: reference <position> <size> <density> <texture> <pitch> <feedback> <reverb> <freeze at> <seconds> <in.f32> <out.f32> [seed]\n");
        return 2;
    }
    const double freeze_at = atof(argv[8]), seconds = atof(argv[9]), rate = 32000;
    std::vector<float> input;
    if (FILE* f = std::fopen(argv[10], "rb")) {
        float x;
        while (std::fread(&x, sizeof(x), 1, f) == 1) input.push_back(x);
        std::fclose(f);
    }
    if (argc > 12) stmlib::Random::Seed(static_cast<uint32_t>(atol(argv[12])));
    processor.Init(large_buffer, sizeof(large_buffer), small_buffer, sizeof(small_buffer));
    processor.set_playback_mode(PLAYBACK_MODE_GRANULAR);
    processor.set_quality(argc > 14 ? atoi(argv[14]) : 0);
    processor.Prepare();
    Parameters* p = processor.mutable_parameters();
    p->position = atof(argv[1]);
    p->size = atof(argv[2]);
    p->density = atof(argv[3]);
    p->texture = atof(argv[4]);
    p->pitch = stmlib::Interpolate(lut_quantized_pitch, atof(argv[5]), 1024.0f);
    p->feedback = atof(argv[6]);
    p->reverb = atof(argv[7]);
    p->dry_wet = 1.0f;
    p->stereo_spread = argc > 13 ? atof(argv[13]) : 0.0f;
    p->freeze = p->trigger = p->gate = false;

    std::vector<float> x;
    const size_t blocks = static_cast<size_t>(seconds * rate / kMaxBlockSize);
    ShortFrame in[kMaxBlockSize], out[kMaxBlockSize];
    for (size_t b = 0; b < blocks; ++b) {
        for (size_t i = 0; i < kMaxBlockSize; ++i) {
            const size_t n = b * kMaxBlockSize + i;
            const float v = n < input.size() ? input[n] : 0.0f;
            in[i].l = in[i].r = static_cast<short>(std::min(std::max(v * 32767.0f, -32768.0f), 32767.0f));
        }
        p->freeze = freeze_at >= 0 && b * kMaxBlockSize >= freeze_at * rate;
        processor.Process(in, out, kMaxBlockSize);
        processor.Prepare();
        for (size_t i = 0; i < kMaxBlockSize; ++i) {
            x.push_back(out[i].l / 32768.0f);
            x.push_back(out[i].r / 32768.0f);
        }
    }
    FILE* f = std::fopen(argv[11], "wb");
    if (!f) return 1;
    std::fwrite(x.data(), sizeof(float), x.size(), f);
    std::fclose(f);
    return 0;
}
