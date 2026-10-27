// Cumulus' sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Each check prints FAIL and the
// measurement when it does not hold.
#include "../fx.cpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace {
using opxy::i32;
using opxy::u32;
const double kRate = 44100, kPi = 3.14159265358979323846;
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
    double position, size, density, texture, pitch, feedback, reverb, freeze;
};
const Settings kDefault = {of99(20), of99(50), of99(80), of99(50), of99(50), 0.0, 0.0, 0.25};

cumulus::State state;
i32 memory[cumulus::kBufferBytes / 4];
const opxy::Buffer kBuffer = {memory, cumulus::kBufferBytes / 4};

// Places the effect on a send: initialize, then the blocks that clear its buffer.
void place() {
    cumulus::initialize(state);
    const i32 parameters[8] = {};
    i32 l[opxy::block_frames], r[opxy::block_frames];
    while (state.cleared < sizeof(cumulus::Clouds)) cumulus::process(state, kBuffer, l, r, parameters);
}

// Sends `seconds` of input through it at Clouds' eight knobs (render): left and
// right as functions of time.
struct Output {
    std::vector<double> left, right;
};
template <class Left, class Right>
Output send(const Settings& s, double seconds, Left in_left, Right in_right, double offset = 0) {
    const i32 parameters[8] = {q31(s.position), q31(s.size),     q31(s.density), q31(s.texture),
                               q31(s.pitch),    q31(s.feedback), q31(s.reverb),  q31(s.freeze)};
    Output out;
    i32 l[opxy::block_frames], r[opxy::block_frames];
    const unsigned blocks = static_cast<unsigned>(seconds * kRate / opxy::block_frames);
    for (unsigned b = 0; b < blocks; ++b) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const double t = offset + (b * opxy::block_frames + i) / kRate;
            l[i] = q31(in_left(t));
            r[i] = q31(in_right(t));
        }
        cumulus::render(state, kBuffer, l, r, parameters);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            out.left.push_back(l[i] / 2147483648.0);
            out.right.push_back(r[i] / 2147483648.0);
        }
    }
    return out;
}
std::function<double(double)> tone(double hz, double amplitude = 0.5) {
    return [=](double t) { return amplitude * std::sin(2 * kPi * hz * t); };
}
double silence(double) { return 0; }

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
double mean(const std::vector<double>& x, double from, double to) {
    const size_t a = static_cast<size_t>(from * kRate), b = std::min(x.size(), static_cast<size_t>(to * kRate));
    double sum = 0;
    for (size_t i = a; i < b; ++i) sum += x[i];
    return sum / (b - a);
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
}  // namespace

int main() {
    // Outside a send the buffer is empty: silence, whatever comes in.
    {
        cumulus::initialize(state);
        const i32 parameters[8] = {};
        i32 l[opxy::block_frames], r[opxy::block_frames];
        for (unsigned i = 0; i < opxy::block_frames; ++i) l[i] = r[i] = 0x40000000;
        const opxy::Buffer none = {nullptr, 0};
        cumulus::process(state, none, l, r, parameters);
        bool quiet = true;
        for (unsigned i = 0; i < opxy::block_frames; ++i) quiet = quiet && !l[i] && !r[i];
        check(quiet, "silence outside a send", 0);
    }

    // The controls play Clouds' knobs: Position, Size, Pitch and Density, then on
    // the same encoders Freeze, Texture and Reverb. Dry is the stock mixer's:
    // whatever it reads, Cumulus plays Clouds without feedback.
    {
        place();
        const Settings s = {of99(30), of99(60), of99(85), of99(30), of99(92), 0.0, of99(30), 0.75};
        const Output knobs = send(s, 1.5, tone(330), tone(330));
        place();
        const i32 controls[8] = {q31(s.position), q31(s.size),    q31(s.pitch), q31(s.density),
                                 q31(s.freeze),   q31(s.texture), q31(0.6),     q31(s.reverb)};
        i32 l[opxy::block_frames], r[opxy::block_frames];
        double difference = 0;
        for (size_t b = 0; b < knobs.left.size() / opxy::block_frames; ++b) {
            for (unsigned i = 0; i < opxy::block_frames; ++i) l[i] = r[i] = q31(tone(330)((b * opxy::block_frames + i) / kRate));
            cumulus::process(state, kBuffer, l, r, controls);
            // process returns Clouds' wet 4 times louder, saturating (fx.cpp).
            for (unsigned i = 0; i < opxy::block_frames; ++i) {
                const double expected = std::fmin(std::fmax(knobs.left[b * opxy::block_frames + i] * 4, -1.0), 1.0);
                difference = std::fmax(difference, std::fabs(l[i] / 2147483648.0 - expected));
            }
        }
        check(difference < 1e-8, "the controls play Clouds' knobs", difference);
    }

    // The buffer holds whatever was there: Cumulus clears it before it sounds,
    // and stays silent meanwhile.
    {
        u32 seed = 12345;
        for (i32& word : memory) word = static_cast<i32>(seed = seed * 1664525u + 1013904223u);
        cumulus::initialize(state);
        const Output clearing = send(kDefault, 0.025, tone(220), tone(220));
        check(peak(clearing.left) == 0 && peak(clearing.right) == 0, "silent while clearing", peak(clearing.left));
        const Output x = send(kDefault, 2.0, tone(220), tone(220));
        check(rms(x.left, 1.2, 2.0) > 0.05, "sounds once cleared", rms(x.left, 1.2, 2.0));
        check(peak(x.left) < 1.0 && peak(x.right) < 1.0, "no garbage from the old buffer", peak(x.left));
    }

    // Silence in, silence out, once the grains and reverb have played out.
    {
        place();
        Settings s = kDefault;
        s.reverb = 0.5;
        send(s, 1.5, tone(330), tone(330));
        const Output x = send(s, 8.0, silence, silence);
        check(rms(x.left, 7.5, 8.0) < 1e-4 && rms(x.right, 7.5, 8.0) < 1e-4, "silence in, silence out",
              rms(x.left, 7.5, 8.0));
        check(std::fabs(mean(x.left, 7.5, 8.0)) < 1e-6, "no offset left behind", mean(x.left, 7.5, 8.0));
    }

    // Grains play the send in tune: at Pitch 50, an octave up at 92, an octave down at 7.
    for (int n : {50, 92, 7}) {
        place();
        Settings s = {of99(20), of99(85), of99(30), of99(60), of99(n), 0.0, 0.0, 0.25};
        const double ratio = n == 50 ? 1 : n == 92 ? 2 : 0.5;
        const Output x = send(s, 2.5, tone(330), tone(330));
        const double cents = grain_cents(x.left, 330 * ratio, 1.1, 2.5);
        if (std::fabs(cents) >= 3) std::printf("  Pitch %d: %+.1f cents\n", n, cents);
        check(std::fabs(cents) < 3, "grains in tune within 3 cents", cents);
    }

    // Density 50 plays no grains: without reverb, nothing returns.
    {
        place();
        Settings s = kDefault;
        s.density = of99(50);
        const Output x = send(s, 1.5, tone(220), tone(220));
        check(rms(x.left, 0.5, 1.5) < 1e-6, "Density 50 plays no grains", rms(x.left, 0.5, 1.5));
    }

    // Each channel's grains play its own recording: a left-only send returns
    // left only, until the reverb mixes them.
    {
        place();
        const Output x = send(kDefault, 1.5, tone(220), silence);
        check(rms(x.left, 1.0, 1.5) > 0.05 && rms(x.right, 1.0, 1.5) < 1e-6, "channels stay apart", rms(x.right, 1.0, 1.5));
    }

    // Freeze holds the recording: frozen on 220 Hz, the send then moves to 330 Hz.
    {
        place();
        Settings s = {of99(30), of99(40), of99(85), of99(60), of99(50), 0.0, 0.0, 0.25};
        send(s, 1.3, tone(220), tone(220));
        s.freeze = 0.75;
        const Output x = send(s, 1.0, tone(330), tone(330), 1.3);
        const double held = magnitude(x.left, 220, 0.3, 1.0), moved = magnitude(x.left, 330, 0.3, 1.0);
        check(held > 10 * moved, "Freeze holds the recording", moved / (held + 1e-12));
    }

    // Feedback repeats, and dies away once the send stops; full Feedback with
    // full Reverb stays within full scale.
    {
        place();
        Settings s = kDefault;
        s.feedback = 0.5;
        send(s, 1.0, tone(440), tone(440));
        const Output x = send(s, 8.0, silence, silence);
        check(rms(x.left, 0.1, 0.4) > 1e-3, "Feedback repeats the send", rms(x.left, 0.1, 0.4));
        check(rms(x.left, 7.5, 8.0) < 1e-4, "and dies away", rms(x.left, 7.5, 8.0));
        check(std::fabs(mean(x.left, 7.5, 8.0)) < 1e-6, "without an offset", mean(x.left, 7.5, 8.0));
        place();
        s = kDefault;
        s.feedback = 1.0;
        s.reverb = 1.0;
        s.density = 1.0;
        s.texture = 1.0;
        const Output loud = send(s, 3.0, tone(110, 1.0), tone(110, 1.0));
        check(peak(loud.left) <= 1.0 && peak(loud.right) <= 1.0, "full Feedback and Reverb stay in range", peak(loud.left));
    }

    // Reverb lengthens the tail after the send stops.
    {
        double previous = 0;
        for (double amount : {0.0, 0.4, 0.8}) {
            place();
            Settings s = kDefault;
            s.reverb = amount;
            send(s, 1.2, tone(440), tone(440));
            const double tail = rms(send(s, 1.5, silence, silence).right, 0.8, 1.2);
            if (amount > 0 && tail <= previous) std::printf("  reverb %g: %g after %g\n", amount, tail, previous);
            check(amount == 0 ? true : tail > previous, "Reverb lengthens the tail", tail);
            previous = tail;
        }
    }

    // An engine reset clears again: silent at first, then the effect resumes.
    {
        place();
        send(kDefault, 1.0, tone(220), tone(220));
        cumulus::initialize(state);
        const Output x = send(kDefault, 0.025, tone(220), tone(220));
        check(peak(x.left) == 0, "silent while clearing after a reset", peak(x.left));
    }

    // A quiet send keeps its detail: 48 dB down, the return is the louder send's,
    // scaled. An OP-XY send arrives far below Clouds' full scale, where a 16-bit
    // recording would leave a few steps of each note.
    {
        place();
        const Output loud = send(kDefault, 2.0, tone(220, 1.0 / 64), tone(220, 1.0 / 64));
        place();
        const Output quiet = send(kDefault, 2.0, tone(220, 1.0 / 16384), tone(220, 1.0 / 16384));
        double error = 0, level = 0;
        for (size_t i = static_cast<size_t>(1.0 * kRate); i < loud.left.size(); ++i) {
            const double d = quiet.left[i] * 256 - loud.left[i];
            error += d * d;
            level += loud.left[i] * loud.left[i];
        }
        const double ratio = std::sqrt(error / level);
        check(ratio < 0.01, "a send 48 dB down keeps its detail", ratio);
    }

    // The return's level, for setting it against the stock effects.
    {
        place();
        const Output x = send(kDefault, 2.0, tone(220), tone(220));
        std::printf("  a 220 Hz send at half scale returns peak %.3f, RMS %.3f\n", peak(x.left, 1.0), rms(x.left, 1.0, 2.0));
    }
    if (!failures) std::puts("PASS cumulus sound checks");
    return failures ? 1 : 0;
}
