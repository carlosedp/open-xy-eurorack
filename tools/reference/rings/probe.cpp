// Chimes, struck once, as 32-bit floats in Rings' units (its output before
// the output scale), with the reference's arguments.
//   probe <model> <hz> <structure> <brightness> <damping> <position> <seconds> <out.f32> [seed]
// <model> is Chimes': 0 modal, 1 sympathetic, 2 string, 3 chord. The voice is
// owner 2, Rings' second voice, which the reference's first strum plays.
#include "engine.cpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

static chimes::Voice voice;
static chimes::Shared shared;

int main(int argc, char** argv) {
    if (argc < 9) {
        std::fprintf(stderr, "usage: probe <model> <hz> <structure> <brightness> <damping> <position> <seconds> <out.f32> [seed]\n");
        return 2;
    }
    const unsigned model = atoi(argv[1]);
    const double hz = atof(argv[2]), seconds = atof(argv[7]), rate = 44100;
    auto q31 = [](double x) { return static_cast<opxy::i32>(x * 2147483647.0); };
    const opxy::i32 parameters[8] = {q31(atof(argv[3])), q31(atof(argv[4])), q31(atof(argv[5])), q31(atof(argv[6])),
                                     q31((2.0 * model + 1) / 8), 0, 0, 0};
    chimes::initialize(voice, shared, 2);
    if (argc > 9) voice.seed = static_cast<opxy::u32>(atol(argv[9]));
    chimes::start(voice, shared, 2);
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = static_cast<opxy::u32>(hz / rate * 4294967296.0);
    std::vector<float> x;
    opxy::i32 out[opxy::block_frames];
    const int blocks = static_cast<int>(seconds * rate / opxy::block_frames);
    for (int b = 0; b < blocks; ++b) {
        chimes::render(voice, shared, 2, out, parameters, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i)
            x.push_back(static_cast<float>(out[i] / (67108864.0 * chimes::kLevel)));
    }
    FILE* f = std::fopen(argv[8], "wb");
    if (!f) return 1;
    std::fwrite(x.data(), sizeof(float), x.size(), f);
    std::fclose(f);
    return 0;
}
