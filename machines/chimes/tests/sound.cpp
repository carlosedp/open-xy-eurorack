// Chimes' sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Each check prints FAIL and the
// measurement when it does not hold.
#include "../engine.cpp"
#include "../display.cpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

namespace {
using opxy::i32;
using opxy::u32;
const double kRate = 44100;
const i32 kPeak = 1073741824;  // definition.json outputPeakQ31
int failures = 0;

void check(bool ok, const char* what, double value) {
    if (!ok) {
        std::printf("FAIL %s (%g)\n", what, value);
        ++failures;
    }
}

i32 q31(double x) { return static_cast<i32>(x * 2147483647.0); }
u32 increment(double hz) { return static_cast<u32>(hz / kRate * 4294967296.0); }
double model_value(unsigned model) { return (2.0 * model + 1) / 8; }

struct Settings {
    unsigned model;
    double structure, brightness, damping, position;
};

chimes::Shared shared;
chimes::Voice voices[20];

// Renders `seconds` of one voice struck at `hz`, as doubles of full scale.
std::vector<double> play(unsigned owner, const Settings& s, double hz, double seconds, bool strike = true,
                         bool fresh = true) {
    chimes::Voice& voice = voices[owner - 1];
    if (fresh) chimes::initialize(voice, shared, owner);
    if (strike) chimes::start(voice, shared, owner);
    const i32 parameters[8] = {q31(s.structure), q31(s.brightness), q31(s.damping), q31(s.position),
                               q31(model_value(s.model)), 0, 0, 0};
    u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = increment(hz);
    std::vector<double> out;
    i32 block[opxy::block_frames];
    const unsigned blocks = static_cast<unsigned>(seconds * kRate / opxy::block_frames);
    for (unsigned b = 0; b < blocks; ++b) {
        chimes::render(voice, shared, owner, block, parameters, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) out.push_back(block[i] / 2147483648.0);
    }
    return out;
}

double peak(const std::vector<double>& x) {
    double p = 0;
    for (double v : x) p = std::fmax(p, std::fabs(v));
    return p;
}
double rms(const std::vector<double>& x, double from, double to) {
    const size_t a = static_cast<size_t>(from * kRate), b = static_cast<size_t>(to * kRate);
    double sum = 0;
    for (size_t i = a; i < b; ++i) sum += x[i] * x[i];
    return std::sqrt(sum / (b - a));
}
double mean(const std::vector<double>& x, double from, double to) {
    const size_t a = static_cast<size_t>(from * kRate), b = static_cast<size_t>(to * kRate);
    double sum = 0;
    for (size_t i = a; i < b; ++i) sum += x[i];
    return sum / (b - a);
}
// Energy of the first difference over the signal's: a brightness measure.
double brightness(const std::vector<double>& x, double from, double to) {
    const size_t a = static_cast<size_t>(from * kRate), b = static_cast<size_t>(to * kRate);
    double d = 0, e = 1e-30;
    for (size_t i = a + 1; i < b; ++i) {
        d += (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        e += x[i] * x[i];
    }
    return d / e;
}
// The frequency of the strongest partial within a semitone of `hz`: the peak of
// a Hann-windowed DFT, searched in 0.2-cent steps. A string's upper partials
// sit a little sharp of harmonic, as Rings' loop filters delay them less; the
// note is the fundamental's.
double partial(const std::vector<double>& x, double from, double seconds, double hz) {
    const size_t a = static_cast<size_t>(from * kRate), n = static_cast<size_t>(seconds * kRate);
    const double pi = 3.141592653589793;
    double best = 0, at = hz;
    for (double c = -100; c <= 100; c += 0.2) {
        const double f = hz * std::pow(2.0, c / 1200), w = 2 * pi * f / kRate;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) {
            const double v = x[a + i] * (0.5 - 0.5 * std::cos(2 * pi * i / (n - 1)));
            re += v * std::cos(w * i);
            im -= v * std::sin(w * i);
        }
        if (re * re + im * im > best) {
            best = re * re + im * im;
            at = f;
        }
    }
    return at;
}
double cents(double hz, double target) { return 1200 * std::log2(hz / target); }
// The level 0.3 s after the strike, against the first 50 ms.
double ringing(const std::vector<double>& x) { return rms(x, 0.28, 0.32) / rms(x, 0, 0.05); }
}  // namespace

int main() {
    const char* names[] = {"modal", "sympathetic", "string", "chord"};
    // Captions read 0..99 as on the stock engines: n / 99 shows n, full travel 99.
    for (int n = 0; n <= 99; ++n) {
        char shown[3], expected[3];
        chimes::caption(shown, q31(n / 99.0));
        std::snprintf(expected, sizeof(expected), "%d", n);
        check(std::strcmp(shown, expected) == 0, "n / 99 reads n", n);
    }
    {
        char shown[3];
        chimes::caption(shown, 0x7fffffff);
        check(std::strcmp(shown, "99") == 0, "full travel reads 99", 99);
    }
    // Silence until struck.
    for (unsigned m = 0; m < chimes::kModels; ++m) {
        const Settings s = {m, 0.5, 0.5, 0.5, 0.5};
        check(peak(play(1, s, 220, 0.2, false)) == 0, "silent before a note", m);
    }

    // Every model sounds, stays within its declared peak, holds no DC and fades.
    // A sympathetic string, damped toward 0.95, rings on for seconds after the
    // first: those models get longer to fade.
    for (unsigned m = 0; m < chimes::kModels; ++m)
        for (double hz : {55.0, 220.0, 880.0, 3520.0})
            for (double structure : {0.0, 0.25, 0.6, 1.0})
                for (double bright : {0.0, 0.5, 1.0}) {
                    const Settings s = {m, structure, bright, 0.5, 0.3};
                    const double seconds = m == chimes::kSympathetic || m == chimes::kChord ? 8 : 2;
                    const std::vector<double> x = play(1, s, hz, seconds);
                    const double p = peak(x), dc = mean(x, 0, seconds);
                    const double end = rms(x, seconds - 0.2, seconds);
                    const bool fades = end < rms(x, 0, 0.2);
                    if (p < 1e-3 || p > kPeak / 2147483648.0 || std::fabs(dc) >= 0.004 || !fades)
                        std::printf("  %s %g Hz structure %g brightness %g: peak %g, DC %g, end/start %g\n",
                                    names[m], hz, structure, bright, p, dc, end / rms(x, 0, 0.2));
                    check(p > 1e-3, "sounds", p);
                    check(p <= kPeak / 2147483648.0, "within the declared peak", p);
                    check(std::fabs(dc) < 0.004, "no DC", dc);
                    check(fades, "fades at damping 0.5", end);
                }

    // In tune: the modal resonator and the string at harmonic Structure.
    for (unsigned m : {static_cast<unsigned>(chimes::kModal), static_cast<unsigned>(chimes::kString)})
        for (double hz : {41.2, 110.0, 261.63, 440.0, 1046.5, 2093.0}) {
            const Settings s = {m, 0.25, 0.5, 0.6, 0.3};
            const std::vector<double> x = play(1, s, hz, 0.6);
            const double c = cents(partial(x, 0.05, 0.25, hz), hz);
            if (std::fabs(c) >= 5) std::printf("  %s at %g Hz: %+.1f cents\n", names[m], hz, c);
            check(std::fabs(c) < 5, "in tune within 5 cents", c);
        }

    // Damping lengthens the ring; Brightness brightens it. Not the ring of the
    // sympathetic and chord models: there, as on Rings, a longer-ringing second
    // string builds up into the limiter, which then holds the whole voice down.
    for (unsigned m = 0; m < chimes::kModels; ++m) {
        double previous = 0;
        for (double damping : {0.3, 0.55, 0.8}) {
            if (m == chimes::kSympathetic || m == chimes::kChord) break;
            const Settings s = {m, 0.4, 0.5, damping, 0.3};
            const double level = ringing(play(1, s, 220, 0.4));
            if (level <= previous) std::printf("  %s damping %g: %g after %g\n", names[m], damping, level, previous);
            check(level > previous, "Damping lengthens the ring", level);
            previous = level;
        }
        double dull = 0;
        for (double bright : {0.1, 0.5, 0.9}) {
            const Settings s = {m, 0.4, bright, 0.6, 0.3};
            const double b = brightness(play(1, s, 220, 0.5), 0.05, 0.4);
            if (b <= dull) std::printf("  %s brightness %g: %g after %g\n", names[m], bright, b, dull);
            check(b > dull, "Brightness brightens", b);
            dull = b;
        }
    }

    // A string rings on through other notes, and a new note can steal its
    // lines: twelve string voices sound, a thirteenth silences the oldest
    // until it is struck again.
    {
        const Settings s = {chimes::kString, 0.3, 0.5, 0.8, 0.3};
        for (unsigned owner = 1; owner <= 12; ++owner) play(owner, s, 110 * owner, 0.05);
        check(peak(play(1, s, 110, 0.1, false, false)) > 0, "a leased voice keeps ringing", 0);
        play(13, s, 2000, 0.05);
        check(peak(play(1, s, 110, 0.1, false, false)) == 0, "a stolen voice falls silent", 1);
        check(peak(play(1, s, 110, 0.2, true, false)) > 1e-3, "and sounds when struck again", 1);
        for (unsigned owner = 1; owner <= 13; ++owner) chimes::destroy(voices[owner - 1], shared, owner);
    }

    // Changing model while ringing stays within the peak.
    {
        chimes::initialize(voices[0], shared, 1);
        for (unsigned m = 0; m < 2 * chimes::kModels; ++m) {
            const Settings s = {m % chimes::kModels, 0.7, 0.8, 0.9, 0.5};
            const std::vector<double> x = play(1, s, 330, 0.3, m % 2 == 0, false);
            check(peak(x) <= kPeak / 2147483648.0, "model changes stay within the peak", peak(x));
        }
        chimes::destroy(voices[0], shared, 1);
    }

    // The levels, for setting the output scale against the core machines.
    for (unsigned m = 0; m < chimes::kModels; ++m) {
        const Settings s = {m, 0.4, 0.6, 0.6, 0.3};
        const std::vector<double> x = play(1, s, 220, 1.0);
        std::printf("  %s: peak %.3f, RMS %.3f over the first second\n", names[m], peak(x), rms(x, 0, 1));
    }
    if (!failures) std::puts("PASS chimes sound checks");
    return failures ? 1 : 0;
}
