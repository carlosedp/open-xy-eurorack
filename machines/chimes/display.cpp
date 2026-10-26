#include "display.h"
#include "modal.h"

// Twelve chimes on a rail: the modal resonator's partials. Structure sets
// their lengths as on real chimes, whose pitch rises as the square of their
// shortening: each chime is as long as its partial, stretched by Structure as
// the sound stretches it, allows. A note swings each chime by the Position
// weight the sound gives its partial, and the swing fades as Damping lets the
// sound fade. Brightness dims the shortest chimes, as it dulls the highest
// partials. Display code only: no audio state.
namespace chimes {
namespace draw = opxy::draw;

const i32 kRail = 40;       // the rail's height on screen, pixels
const i32 kCord = 6;        // each chime hangs this far below the rail
const i32 kLongest = 114;   // the fundamental's chime, pixels
const i32 kShortest = 12;
const i32 kSpacing = 40;    // between chimes, pixels; three to each outline
const i32 kMostSwing = 60;  // tenths of a degree, at full Position weight

const char* const kModelNames[kModels] = {"modal", "symp", "string", "chord"};
const char* const kChordNames[11] = {"oct-", "2nd", "min3", "maj3", "4th", "5th",
                                     "6th",  "min7", "maj7", "oct",  "oct+-"};

// One chime's outline, hanging from (0, 0), in Q10 pixels before its swing: a
// cord down one side, the tube down and around its rounded foot, and back up.
// The y values below kCord grow with the chime's length.
struct Corner { i32 x, y; bool foot; };
const Corner kChime[11] = {{-512, 0, false},   {-512, 6144, false}, {-2560, 6144, false}, {-2560, 0, true},
                           {-1843, 1843, true}, {0, 2560, true},     {1843, 1843, true},   {2560, 0, true},
                           {2560, 6144, false}, {512, 6144, false},  {512, 0, false}};

inline u32 square_root(u32 x) {
    u32 root = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= root + bit) {
            x -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

// The chimes' lengths, Q10 pixels, from the partials as tune_modes stretches them.
inline void chime_lengths(i32 structure, i32* out) {
    i32 stiffness = lookup(kStiffness, structure);
    u32 stretch = 1u << 26;  // Q26
    for (unsigned i = 0; i < kTubes; ++i) {
        const u32 root = square_root((i + 1) * (stretch >> 10));  // sqrt of the partial's ratio, Q8
        const i32 length = static_cast<i32>((static_cast<u32>(kLongest) << 18) / (root ? root : 1));
        out[i] = clamp(length, kShortest * draw::unit, kLongest * draw::unit);
        stretch = static_cast<u32>(static_cast<i32>(stretch) + stiffness);
        stiffness = mul31(stiffness, stiffness < 0 ? 1997159793 : 2104533975);  // 0.93, 0.98
    }
}

inline void display_reset(DisplayState& state) {
    for (unsigned i = 0; i < kTubes; ++i) state.swing[i] = state.phase[i] = 0;
    state.serial = state.track = state.initialized = 0;
    state.structure = -1;
    state.key = 1;
}

inline void display(DisplayState& state, const draw::Input& input, Frame& frame) {
    const i32 unit = draw::unit;
    const i32 structure = control(input.slots[0]), brightness = control(input.slots[1]);
    const i32 damping = control(input.slots[2]), position = control(input.slots[3]);
    const u32 model = (static_cast<u32>(control(input.slots[4])) >> 16) * kModels >> 15;
    const u32 chord = ((static_cast<u32>(structure) >> 8) * 10 + (1u << 22)) >> 23;

    // The first encoder names the model, or in the chord model the chord.
    for (u32 p = 0; p < opxy::page_controls; ++p) draw::header(frame, p, "");
    if (input.shown[0] == 4) {
        draw::header(frame, 0, kModelNames[model]);
    } else if (model == kChord) {
        draw::header(frame, 0, kChordNames[chord]);
    }

    // A note swings each chime by its partial's Position weight, adding to its swing.
    const i32 elapsed = clamp(input.elapsed, 0, 205);  // at most 0.2 s per frame
    bool was_moving = false;
    for (unsigned i = 0; i < kTubes; ++i) was_moving = was_moving || state.swing[i];
    if (!state.initialized || state.track != input.track || input.reduced_motion) {
        for (unsigned i = 0; i < kTubes; ++i) state.swing[i] = 0;
    } else if (input.note_serial != state.serial) {
        i32 weights[kTubes];
        position_weights(weights, kTubes, position);
        for (unsigned i = 0; i < kTubes; ++i) {
            state.swing[i] = clamp(state.swing[i] + mul31(weights[i], kMostSwing * unit), 0, kMostSwing * unit);
            state.phase[i] = 0;
        }
    }
    state.initialized = 1;
    state.track = input.track;
    state.serial = input.note_serial;

    i32 lengths[kTubes];
    chime_lengths(structure, lengths);
    // The swing halves 0.25 times a second at full Damping, up to 4 times at none.
    const i32 octaves_per_second = 16384 + mul31(kMaximum - damping, 245760);  // Q16
    const i32 decay = clamp31(exp2(-(octaves_per_second * elapsed / 1024), 31));
    bool moving = false;
    for (unsigned i = 0; i < kTubes; ++i) {
        state.swing[i] = mul31(state.swing[i], decay);
        if (state.swing[i] < unit) state.swing[i] = 0;  // under a tenth of a degree
        // A pendulum: 0.9 Hz for the longest chime, faster as the square root of its shortening.
        const u32 rate = square_root(static_cast<u32>(kLongest * unit * 1024 / lengths[i]));  // Q5
        state.phase[i] += rate * static_cast<u32>(elapsed) * 117965u;  // 0.9 x 2^17
        moving = moving || state.swing[i];
    }

    // Each outline: its piece of the rail, a 3-pixel bar, with its three chimes
    // hanging from it, each turned about its cord by its swing.
    const i32 top = -60 * unit;
    const u32 lit = (static_cast<u32>(brightness) >> 16) * 5 >> 15;  // chime groups lit, 0..4
    for (u32 p = 0; p < Frame::path_count; ++p) {
        Frame::path_type& path = frame.paths[p];
        draw::Point* points = path.points;
        points[0].x = -63 * unit;
        points[0].y = top;
        const i32 x0 = (60 + 120 * static_cast<i32>(p)) * unit, y0 = (kRail + 60) * unit;
        for (u32 k = 0; k < 3; ++k) {
            const u32 i = 3 * p + k;
            const i32 tenths = draw::floor_q10(draw::floor_q10(
                state.swing[i] * draw::sine(static_cast<i32>(((state.phase[i] >> 16) * 3600) >> 16))));
            const i32 s = draw::sine(tenths), c = draw::sine(tenths + 900);
            const i32 hang = (static_cast<i32>(k) - 1) * kSpacing * unit;
            for (u32 j = 0; j < 11; ++j) {
                const i32 x = kChime[j].x, y = kChime[j].y + (kChime[j].foot ? kCord * unit + lengths[i] : 0);
                draw::Point& point = points[1 + 11 * k + j];
                point.x = hang + draw::floor_q10(x * c - y * s);
                point.y = top + draw::floor_q10(x * s + y * c);
            }
        }
        points[34].x = 63 * unit;
        points[34].y = top;
        points[35].x = 63 * unit;
        points[35].y = top - 3 * unit;
        points[36].x = 21 * unit;
        points[36].y = top - 3 * unit;
        points[37].x = -21 * unit;
        points[37].y = top - 3 * unit;
        points[38].x = -63 * unit;
        points[38].y = top - 3 * unit;
        // Brightness lights the chimes from the longest: the shortest dim first.
        const u32 shade = p < lit ? 0 : p == lit ? 1 : 2;
        draw::place(frame, p, x0, y0, unit, shade, draw::stroke);
    }
    // New geometry while the chimes move, as they come to rest and when Structure moves.
    if (moving || was_moving || structure != state.structure) state.key = state.key % 0x7ffffffeu + 1;
    state.structure = structure;
    for (u32 p = 0; p < Frame::path_count; ++p) frame.paths[p].key = state.key;
}
}  // namespace chimes
