// Cirrus' sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Each check prints FAIL and the
// measurement when it does not hold.
#include "../sample.cpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace {
using opxy::i32;
using opxy::u32;
const double kRate = 44100, kPi = 3.14159265358979323846;
const u32 kMiddleCIncrement = 25480119;  // middle C's phase increment at 44.1 kHz
int failures = 0;

void check(bool ok, const char* what, double value) {
    if (!ok) {
        std::printf("FAIL %s (%g)\n", what, value);
        ++failures;
    }
}

i32 q31(double x) { return static_cast<i32>(std::fmin(std::fmax(x, -1.0), 1.0) * 2147483647.0); }
double of99(int n) { return n / 99.0; }

struct Settings {
    double position, size, density, texture, pitch, speed, spread;
};
const Settings kDefault = {0, of99(50), of99(80), of99(50), of99(50), of99(75), of99(50)};
// One grain, the note's own, at its longest: half a second, in which to
// measure its pitch. Overlapping grains of a transposed tone sum out of phase.
const Settings kSingle = {0, 1.0, of99(50), of99(60), of99(50), of99(50), 0};
const Settings kStill = {0, of99(40), of99(30), of99(60), of99(50), of99(50), 0};

// A sample: frames of channels, each a function of time in seconds.
struct Sample {
    std::vector<opxy::i16> data;
    opxy::SampleView view;
};
Sample make(double seconds, u32 rate, std::function<double(double)> left,
            std::function<double(double)> right = nullptr) {
    Sample s;
    const u32 frames = static_cast<u32>(seconds * rate), channels = right ? 2 : 1;
    for (u32 i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        s.data.push_back(static_cast<opxy::i16>(std::lround(32767 * left(t))));
        if (right) s.data.push_back(static_cast<opxy::i16>(std::lround(32767 * right(t))));
    }
    s.view = opxy::SampleView{s.data.empty() ? nullptr : s.data.data(), frames, rate, channels};
    return s;
}
std::function<double(double)> tone(double hz, double amplitude = 0.5) {
    return [=](double t) { return amplitude * std::sin(2 * kPi * hz * t); };
}
double silence(double) { return 0; }

struct Note {
    u32 increment = kMiddleCIncrement;
    i32 cents = 0;  // the pad's tuning
    u32 level = opxy::sample_unity_level;
    u32 begin = 0, end = 0xffffffffu;
};

struct Output {
    std::vector<double> left, right;
};
// Plays a note on a fresh voice for `seconds`.
Output play(const Sample& sample, const Settings& s, double seconds, const Note& note = Note(),
            cirrus::Voice* reuse = nullptr) {
    static cirrus::Voice fresh;
    cirrus::Voice& voice = reuse ? *reuse : fresh;
    if (!reuse) cirrus::initialize(voice, sample.view);
    cirrus::start(voice);
    const i32 parameters[8] = {q31(s.position), q31(s.size),  q31(s.density), q31(s.texture),
                               q31(s.pitch),    q31(s.speed), q31(s.spread),  0};
    u32 pitch[opxy::block_frames];
    for (u32& p : pitch) p = note.increment;
    i32 out[opxy::block_frames * 2];
    const u32 end = note.end < sample.view.frames ? note.end : sample.view.frames;
    const opxy::SampleBlock block{out,
                                  parameters,
                                  pitch,
                                  opxy::Transport{1200, 0, false},
                                  opxy::SampleRegion{note.begin, end, note.level, false, note.cents},
                                  nullptr,
                                  0,
                                  2,
                                  0,
                                  0};
    Output o;
    const unsigned blocks = static_cast<unsigned>(seconds * kRate / opxy::block_frames);
    for (unsigned b = 0; b < blocks; ++b) {
        cirrus::render_sample(voice, block);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            o.left.push_back(out[i * 2] / 2147483648.0);
            o.right.push_back(out[i * 2 + 1] / 2147483648.0);
        }
    }
    return o;
}

double peak(const std::vector<double>& x, double from = 0) {
    double p = 0;
    for (size_t i = static_cast<size_t>(from * kRate); i < x.size(); ++i) p = std::fmax(p, std::fabs(x[i]));
    return p;
}
double rms(const std::vector<double>& x, double from, double to) {
    const size_t a = static_cast<size_t>(from * kRate), b = std::min(x.size(), static_cast<size_t>(to * kRate));
    double sum = 0;
    for (size_t i = a; i < b; ++i) sum += x[i] * x[i];
    return std::sqrt(sum / (b - a));
}
double difference(const std::vector<double>& x, const std::vector<double>& y, double from, double to) {
    const size_t a = static_cast<size_t>(from * kRate), b = std::min(x.size(), static_cast<size_t>(to * kRate));
    double sum = 0;
    for (size_t i = a; i < b; ++i) sum += (x[i] - y[i]) * (x[i] - y[i]);
    return std::sqrt(sum / (b - a));
}
double magnitude(const std::vector<double>& x, double hz, double from, double to) {
    const size_t a = static_cast<size_t>(from * kRate), b = static_cast<size_t>(to * kRate);
    double re = 0, im = 0;
    for (size_t i = a; i < b; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * (i - a) / (b - a));
        re += x[i] * w * std::cos(2 * kPi * hz * i / kRate);
        im += x[i] * w * std::sin(2 * kPi * hz * i / kRate);
    }
    return std::sqrt(re * re + im * im) / (b - a);
}
// The level of the components from lo to hi Hz: grains that repeat one
// stretch of a tone have a comb of components around it, not one.
double band(const std::vector<double>& x, double lo, double hi, double from, double to) {
    double sum = 0;
    for (double hz = lo; hz <= hi; hz += 1) sum += magnitude(x, hz, from, to);
    return sum;
}
// The pitch inside the grains, in cents from hz: the median period between
// upward zero crossings where the output is loud.
double grain_cents(const std::vector<double>& x, double hz, double from, double to) {
    const size_t a = static_cast<size_t>(from * kRate), b = static_cast<size_t>(to * kRate);
    const double loud = 0.3 * peak(std::vector<double>(x.begin() + a, x.begin() + b));
    std::vector<double> periods;
    double last = -1;
    for (size_t i = a; i + 1 < b; ++i) {
        if (x[i] < 0 && x[i + 1] >= 0) {
            const double crossing = i + x[i] / (x[i] - x[i + 1]);
            double level = 0;
            for (size_t j = i > 100 ? i - 100 : 0; j < i + 100 && j < b; ++j) level = std::fmax(level, std::fabs(x[j]));
            if (last >= 0 && level > loud) {
                const double period = crossing - last;
                if (period > 0.8 * kRate / hz && period < 1.25 * kRate / hz) periods.push_back(period);
            }
            last = crossing;
        }
    }
    if (periods.empty()) return 1e9;
    std::sort(periods.begin(), periods.end());
    return 1200 * std::log2(kRate / hz / periods[periods.size() / 2]);
}
void in_tune(const Output& x, double hz, const char* what) {
    const double cents = grain_cents(x.left, hz, 0.02, 0.5);
    if (std::fabs(cents) >= 3) std::printf("  %s: %+.1f cents from %g Hz\n", what, cents, hz);
    check(std::fabs(cents) < 3, what, cents);
}
// 220 Hz for its first second, 330 Hz after.
double halves(double t) { return t < 1.0 ? 0.5 * std::sin(2 * kPi * 220 * t) : 0.5 * std::sin(2 * kPi * 330 * t); }
}  // namespace

int main() {
    const Sample mono = make(2.0, 44100, tone(220));

    // No sample, or a region too short for a grain: silence.
    {
        const Sample none = make(0, 44100, silence);
        check(peak(play(none, kDefault, 0.2).left) == 0, "silence without a sample", 0);
        Note note;
        note.end = 100;
        check(peak(play(mono, kDefault, 0.2, note).left) == 0, "silence from a region of 100 frames", 0);
    }

    // The note sets the pitch: middle C plays the sample as it is. So do the
    // pad's tuning, the sample's own rate and Pitch.
    {
        in_tune(play(mono, kSingle, 0.6), 220, "middle C plays the sample as it is");
        Note up;
        up.increment = kMiddleCIncrement * 2;
        in_tune(play(mono, kSingle, 0.6, up), 440, "an octave up plays an octave up");
        Note tuned;
        tuned.cents = -700;
        in_tune(play(mono, kSingle, 0.6, tuned), 220 * std::pow(2, -7 / 12.0), "the pad's tuning");
        in_tune(play(make(2.0, 22050, tone(220)), kSingle, 0.6), 220, "a sample at 22.05 kHz");
        for (int n : {92, 86, 7}) {
            Settings s = kSingle;
            s.pitch = of99(n);
            const double semitones = n == 92 ? 12 : n == 86 ? 7 : -12;
            in_tune(play(mono, s, 0.6), 220 * std::pow(2, semitones / 12), "Pitch's notches");
        }
    }

    // A note seeds a grain at once; at Density 50 that is the only one.
    {
        Settings s = kDefault;
        s.density = of99(50);
        const Output x = play(mono, s, 1.5);
        check(rms(x.left, 0, 0.1) > 0.02, "a note seeds a grain at once", rms(x.left, 0, 0.1));
        check(rms(x.left, 1.0, 1.5) < 1e-6, "and Density 50 seeds no more", rms(x.left, 1.0, 1.5));
    }

    // Position places the grains: in a sample of 220 Hz then 330 Hz, held still,
    // 0 plays the first and 99 the second.
    {
        const Sample two = make(2.0, 44100, halves);
        Settings s = kStill;
        const Output start = play(two, s, 1.5);
        s.position = 1.0;
        const Output end = play(two, s, 1.5);
        check(band(start.left, 190, 250, 0.3, 1.5) > 10 * band(start.left, 300, 360, 0.3, 1.5), "Position 0 plays the start", 0);
        check(band(end.left, 300, 360, 0.3, 1.5) > 10 * band(end.left, 190, 250, 0.3, 1.5), "Position 99 plays the end", 0);

        // Speed scans: at 99, the sample's own speed, the grains reach its
        // second half after a second; at 50 they stay; at 1 they go back.
        s.position = 0;
        s.speed = 1.0;
        const Output forward = play(two, s, 1.8);
        check(band(forward.left, 300, 360, 1.25, 1.75) > 5 * band(forward.left, 190, 250, 1.25, 1.75), "Speed 99 moves on", 0);
        s.speed = of99(50);
        const Output still = play(two, s, 1.8);
        check(band(still.left, 190, 250, 1.25, 1.75) > 10 * band(still.left, 300, 360, 1.25, 1.75), "Speed 50 stays", 0);
        s.position = 1.0;
        s.speed = of99(1);
        const Output back = play(two, s, 1.8);
        check(band(back.left, 190, 250, 1.25, 1.75) > 5 * band(back.left, 300, 360, 1.25, 1.75), "Speed 1 goes back", 0);
    }

    // Spread: a mono sample sits in the middle at 0 and spreads with it; a
    // stereo sample keeps its channels apart at 0.
    {
        Settings s = kDefault;
        s.spread = 0;
        const Output centred = play(mono, s, 1.5);
        check(difference(centred.left, centred.right, 0, 1.5) < 1e-6, "a mono sample in the middle", 0);
        s.spread = 1.0;
        const Output spread = play(mono, s, 1.5);
        check(difference(spread.left, spread.right, 0.3, 1.5) > 0.02, "Spread moves it apart", 0);
        const Sample left_only = make(2.0, 44100, tone(220), silence);
        s.spread = 0;
        const Output apart = play(left_only, s, 1.5);
        check(rms(apart.left, 0.3, 1.5) > 0.05 && rms(apart.right, 0.3, 1.5) < 1e-6, "a stereo sample's channels stay apart",
              rms(apart.right, 0.3, 1.5));
    }

    // Grains that scan at the sample's own speed keep its spectrum: noise comes
    // out as bright as it went in. Grains a few samples out of step would sum
    // into a comb that dulls the highs.
    {
        u32 state = 3;
        const Sample noise = make(3.0, 44100, [&state](double) { return (static_cast<i32>(state = state * 1664525u + 1013904223u) >> 8) / 33554432.0; });
        Settings s = kDefault;
        s.speed = 1.0;
        s.spread = 0;
        const Output x = play(noise, s, 2.5);
        const double tilt = 20 * std::log10(band(x.left, 4000, 4400, 0.5, 2.5) / band(x.left, 300, 700, 0.5, 2.5));
        check(std::fabs(tilt) < 1.0, "noise keeps its spectrum, within 1 dB", tilt);
    }

    // Each note's grains are its own: two voices scatter differently over noise.
    {
        u32 state = 1;
        const Sample noise = make(2.0, 44100, [&state](double) { return (static_cast<i32>(state = state * 1664525u + 1013904223u) >> 8) / 16777216.0; });
        static cirrus::Voice a, b;
        cirrus::initialize(a, noise.view);
        cirrus::initialize(b, noise.view);
        const Output x = play(noise, kDefault, 1.0, Note(), &a), y = play(noise, kDefault, 1.0, Note(), &b);
        const double apart = difference(x.left, y.left, 0.2, 1.0) / rms(x.left, 0.2, 1.0);
        check(apart > 0.05, "two notes scatter differently", apart);
    }

    // Level: the pad's level scales the output, and a full-scale sample at the
    // densest stays within half scale.
    {
        const Output unity = play(mono, kDefault, 1.5);
        Note half;
        half.level = opxy::sample_unity_level / 2;
        const double ratio = rms(play(mono, kDefault, 1.5, half).left, 0.3, 1.5) / rms(unity.left, 0.3, 1.5);
        check(std::fabs(ratio - 0.5) < 0.02, "the pad's level", ratio);
        Settings s = kDefault;
        s.density = 1.0;
        s.texture = 1.0;
        const Output loud = play(make(2.0, 44100, tone(110, 1.0)), s, 1.5);
        check(peak(loud.left) <= 0.5 && peak(loud.right) <= 0.5, "within half scale", peak(loud.left));
        std::printf("  a 220 Hz sample at half scale plays at peak %.3f, RMS %.3f\n", peak(unity.left, 0.3),
                    rms(unity.left, 0.3, 1.5));
    }

    // A silent sample plays silence, without an offset.
    {
        const Output x = play(make(2.0, 44100, silence), kDefault, 1.0);
        check(peak(x.left) == 0 && peak(x.right) == 0, "a silent sample plays silence", peak(x.left));
    }
    if (!failures) std::puts("PASS cirrus sound checks");
    return failures ? 1 : 0;
}
