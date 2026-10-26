// Upstream Rings (float, 48 kHz) at polyphony 4 with the internal exciter:
// one strum, both outputs summed, as 32-bit floats in Rings' units.
//   reference <model> <hz> <structure> <brightness> <damping> <position> <seconds> <out.f32> [seed]
// <model> is Rings' ResonatorModel: 0 modal, 1 sympathetic, 2 string, 4 quantized
// sympathetic (Chimes' chord). The tonic is the note, as Chimes treats it.
#include "rings/dsp/part.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace rings;
static uint16_t reverb_buffer[32768];
static Part part;

int main(int argc, char** argv) {
    if (argc < 9) {
        std::fprintf(stderr, "usage: reference <model> <hz> <structure> <brightness> <damping> <position> <seconds> <out.f32> [seed]\n");
        return 2;
    }
    const double hz = atof(argv[2]), seconds = atof(argv[7]);
    Patch patch;
    patch.structure = atof(argv[3]);
    patch.brightness = atof(argv[4]);
    patch.damping = atof(argv[5]);
    patch.position = atof(argv[6]);
    if (argc > 9) stmlib::Random::Seed(static_cast<uint32_t>(atol(argv[9])));
    part.Init(reverb_buffer);
    part.set_polyphony(4);
    part.set_model(static_cast<ResonatorModel>(atoi(argv[1])));
    PerformanceState ps = {};
    ps.internal_exciter = true;
    ps.tonic = 69 + 12 * std::log2(hz / 440.0);
    ps.chord = static_cast<int32_t>(patch.structure * 10 + 0.5);
    float in[kMaxBlockSize] = {0}, out[kMaxBlockSize], aux[kMaxBlockSize];
    // Let the note filter and the strings' glides settle before the strum.
    for (int b = 0; b < 200; ++b) part.Process(ps, patch, in, out, aux, kMaxBlockSize);
    std::vector<float> x;
    const int blocks = static_cast<int>(seconds * kSampleRate / kMaxBlockSize);
    for (int b = 0; b < blocks; ++b) {
        ps.strum = b == 0;
        part.Process(ps, patch, in, out, aux, kMaxBlockSize);
        for (size_t i = 0; i < kMaxBlockSize; ++i) x.push_back(out[i] + aux[i]);
    }
    FILE* f = std::fopen(argv[8], "wb");
    if (!f) return 1;
    std::fwrite(x.data(), sizeof(float), x.size(), f);
    std::fclose(f);
    return 0;
}
