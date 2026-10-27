// Cirrus, through its sample.cpp: one note at middle C, where it plays the
// sample at its own pitch, held for <seconds>. Its output, interleaved left and
// right, as 32-bit floats in Clouds' units (Cirrus plays Clouds' full scale at
// half the OP-XY's). <in.f32> holds the sample at 44.1 kHz, played from a mono
// sample (channels 1) or the same on both sides of a stereo one (2).
//   cirrus <position> <size> <density> <texture> <pitch> <speed> <spread> <channels> <seconds> <in.f32> <out.f32> [seed]
#include "sample.cpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static cirrus::Voice voice;

int main(int argc, char** argv) {
    if (argc < 12) {
        std::fprintf(stderr, "usage: cirrus <position> <size> <density> <texture> <pitch> <speed> <spread> <channels> <seconds> <in.f32> <out.f32> [seed]\n");
        return 2;
    }
    using cirrus::i32;
    const unsigned channels = atoi(argv[8]) == 1 ? 1 : 2;
    const double seconds = atof(argv[9]), rate = 44100;
    std::vector<opxy::i16> sample;
    if (FILE* f = std::fopen(argv[10], "rb")) {
        float x;
        while (std::fread(&x, sizeof(x), 1, f) == 1)
            for (unsigned c = 0; c < channels; ++c)
                sample.push_back(static_cast<opxy::i16>(std::lround(std::fmin(std::fmax(x * 32767.0, -32768.0), 32767.0))));
        std::fclose(f);
    }
    auto q31 = [](double x) { return static_cast<i32>(std::fmin(std::fmax(x, -1.0), 1.0) * 2147483647.0); };
    i32 parameters[8] = {};
    for (unsigned i = 0; i < 7; ++i) parameters[i] = q31(atof(argv[1 + i]));
    const opxy::SampleView view = {sample.data(), static_cast<opxy::u32>(sample.size() / channels), 44100, channels};
    cirrus::initialize(voice, view);
    if (argc > 12) voice.seed = static_cast<opxy::u32>(atol(argv[12]));
    cirrus::start(voice);

    opxy::u32 pitch[opxy::block_frames];
    for (opxy::u32& p : pitch) p = 25480119;  // middle C
    i32 out[opxy::block_frames * 2];
    const opxy::SampleBlock block = {out, parameters, pitch, opxy::Transport{1200, 0, false},
                                     opxy::SampleRegion{0, view.frames, opxy::sample_unity_level, false, 0},
                                     nullptr, 0, 2, 0, 0};
    std::vector<float> x;
    const size_t blocks = static_cast<size_t>(seconds * rate / opxy::block_frames);
    for (size_t b = 0; b < blocks; ++b) {
        cirrus::render_sample(voice, block);
        for (size_t i = 0; i < opxy::block_frames * 2; ++i) x.push_back(static_cast<float>(out[i] / 1073741824.0));
    }
    FILE* f = std::fopen(argv[11], "wb");
    if (!f) return 1;
    std::fwrite(x.data(), sizeof(float), x.size(), f);
    std::fclose(f);
    return 0;
}
