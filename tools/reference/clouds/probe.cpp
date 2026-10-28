// Stratus, through its fx.cpp at all of Clouds' knobs (render), with the reference's arguments: its return,
// interleaved left and right, as 32-bit floats in Clouds' units (full scale is
// 1.0 on both sides). <in.f32> holds the input at 44.1 kHz, sent on both
// channels at full scale. The effect clears its buffer for its first blocks,
// then starts from silence as Clouds does.
//   probe <position> <size> <density> <texture> <pitch> <feedback> <reverb> <freeze at> <seconds> <in.f32> <out.f32> [seed]
#include "fx.cpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static stratus::State state;
static opxy::i32 memory[stratus::kBufferBytes / 4];

int main(int argc, char** argv) {
    if (argc < 12) {
        std::fprintf(stderr, "usage: probe <position> <size> <density> <texture> <pitch> <feedback> <reverb> <freeze at> <seconds> <in.f32> <out.f32> [seed]\n");
        return 2;
    }
    using stratus::i32;
    const double freeze_at = atof(argv[8]), seconds = atof(argv[9]), rate = 44100;
    std::vector<float> input;
    if (FILE* f = std::fopen(argv[10], "rb")) {
        float x;
        while (std::fread(&x, sizeof(x), 1, f) == 1) input.push_back(x);
        std::fclose(f);
    }
    auto q31 = [](double x) { return static_cast<i32>(std::fmin(std::fmax(x, -1.0), 1.0) * 2147483647.0); };
    i32 parameters[8];
    for (unsigned i = 0; i < 7; ++i) parameters[i] = q31(atof(argv[1 + i]));
    const opxy::Buffer buffer = {memory, stratus::kBufferBytes / 4};
    stratus::initialize(state);
    // Clear the buffer before the input starts, as the effect does when it is placed.
    i32 left[opxy::block_frames], right[opxy::block_frames];
    while (state.cleared < sizeof(stratus::Clouds)) stratus::process(state, buffer, left, right, parameters);
    if (argc > 12) reinterpret_cast<stratus::Clouds*>(memory)->seed = static_cast<opxy::u32>(atol(argv[12]));

    std::vector<float> x;
    const size_t blocks = static_cast<size_t>(seconds * rate / opxy::block_frames);
    for (size_t b = 0; b < blocks; ++b) {
        for (size_t i = 0; i < opxy::block_frames; ++i) {
            const size_t n = b * opxy::block_frames + i;
            left[i] = right[i] = q31(n < input.size() ? input[n] : 0.0);
        }
        parameters[stratus::kFreeze] = q31(freeze_at >= 0 && b * opxy::block_frames >= freeze_at * rate ? 0.75 : 0.25);
        stratus::render(state, buffer, left, right, parameters);
        for (size_t i = 0; i < opxy::block_frames; ++i) {
            x.push_back(static_cast<float>(left[i] / 2147483648.0));
            x.push_back(static_cast<float>(right[i] / 2147483648.0));
        }
    }
    FILE* f = std::fopen(argv[11], "wb");
    if (!f) return 1;
    std::fwrite(x.data(), sizeof(float), x.size(), f);
    std::fclose(f);
    return 0;
}
