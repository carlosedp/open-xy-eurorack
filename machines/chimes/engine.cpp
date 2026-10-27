// Chimes: Rings' resonator, in fixed point, as an OP-XY voice. Each voice is
// one of Rings' voices at polyphony 4 (see core.h); the OP-XY supplies the
// polyphony and each note strums it with Rings' internal exciter. The stock
// envelope and level follow, so the voice's own ringing is the note.
#include "core.h"
#include "modal.h"
#include "strings.h"
#include <cstring>

namespace chimes {
// Rings' per-model gains into its limiter: modal, sympathetic, string, chord. Q28.
const i32 kModelGain[kModels] = {375809638, 268435456, 375809638, 268435456};
const i32 kAttack = 116607450, kRelease = 46748;  // Rings' limiter slopes, at 44.1 kHz. Q31
const i32 kLimit = 1717986918;                     // 0.8, Q31
// The voice, limited softly to Rings' full scale (1.0), as half of the OP-XY's
// full scale: definition.json declares 2^26 x kLevel. Peaks then come out about
// as loud as Karplus'.
const i32 kLevel = 16;

inline u32 model_of(i32 word) { return (static_cast<u32>(control(word)) >> 16) * kModels >> 15; }

// Back to rest, as Part::ConfigureResonators on a change of model.
inline void reset(Voice& voice, Lines* lines) {
    const u32 lease = voice.lease, write = voice.write, seed = voice.seed;
    std::memset(static_cast<void*>(&voice), 0, sizeof(voice));
    voice.lease = lease;
    voice.write = write;
    voice.seed = seed;
    voice.peak = kUnit / 2;
    if (lines) std::memset(static_cast<void*>(lines), 0, sizeof(*lines));
}

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void destroy(Voice& voice, Shared& shared, u32 owner) {
    shared.lines.release(owner, voice.lease);
    voice.lease = 0;
}
inline void initialize(Voice& voice, Shared& shared, u32 owner) {
    destroy(voice, shared, owner);
    std::memset(static_cast<void*>(&voice), 0, sizeof(voice));
    voice.model = kModels;
    voice.seed = owner * 2654435761u | 1u;
    voice.peak = kUnit / 2;
}
inline void start(Voice& voice, Shared&, u32) { voice.strum = 1; }

// Controls: Structure, Brightness, Damping, Position, then Model (on the first encoder).
inline void render(Voice& voice, Shared& shared, u32 owner, opxy::Output output, opxy::Parameters parameters,
                   opxy::Pitch pitch) {
    const u32 model = model_of(parameters[4]);
    const i32 structure = control(parameters[0]), brightness = control(parameters[1]);
    const i32 damping = control(parameters[2]), position = control(parameters[3]);
    u32 frequency = pitch[0];
    if (frequency < 1u << 16) frequency = 1u << 16;
    if (frequency > 2u * kNyquistLimit) frequency = 2u * kNyquistLimit;
    const bool strum = voice.strum != 0;
    voice.strum = 0;

    Lines* lines = nullptr;
    if (model != kModal) {
        if (strum || model != voice.model) voice.lease = shared.lines.acquire(owner, voice.lease);
        lines = shared.lines.get(owner, voice.lease);
    } else if (voice.lease) {
        shared.lines.release(owner, voice.lease);
        voice.lease = 0;
    }
    if (model != voice.model) {
        reset(voice, lines);
        voice.model = model;
    }
    if (model != kModal && !lines) {
        // The lines went to a newer note; stay silent until struck again.
        for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }

    // The voice as Rings sends one out at polyphony 4: its odd partials less
    // its even ones, or its strings less their pickups (Part::Process).
    i32 out[opxy::block_frames], aux[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) out[i] = aux[i] = 0;
    if (model == kModal) {
        render_modal(voice, frequency, structure, brightness, damping, position, strum, out);
    } else {
        render_strings(voice, *lines, model, (owner - 1) & 3, frequency, structure, brightness, damping, position,
                       strum, out, aux);
        for (unsigned i = 0; i < opxy::block_frames; ++i) out[i] -= aux[i];
    }

    // First the AC coupling of Rings' outputs (3.4 Hz): a string's slow DC mode,
    // which a pluck can leave ringing, stays out of the output. Then
    // Limiter::Process after the model's gain: 0.8 / peak once the peak passes
    // 1.0. With one voice on one output, Rings' stereo peak, the largest of
    // left, right and their difference, is this voice's. Here the peak is
    // measured before the gain, and the scale, which includes the gain,
    // follows it every 8 samples. Each stage is a pass over the block, which
    // leaves the Blackfin's registers enough for it.
    DcBlocker coupling = voice.coupling;
    for (unsigned i = 0; i < opxy::block_frames; ++i) out[i] = coupling.process<11>(out[i]);
    voice.coupling = coupling;
    const i32 gain = kModelGain[model];
    const u32 threshold = reciprocal(static_cast<u32>(gain), 54);  // 1 / gain, Q26
    i32 peak = voice.peak;
    for (unsigned i = 0; i < opxy::block_frames; i += 8) {
        const i32 scale = static_cast<u32>(peak) <= threshold
                              ? mul31(gain, kLimit)  // Q28
                              : static_cast<i32>(multiply_shift(reciprocal(static_cast<u32>(peak), 57), kLimit, 34));
        for (unsigned j = i; j < i + 8; ++j) {
            const i32 x = out[j], level = absolute(x);
            peak += mul31(level - peak, level > peak ? kAttack : kRelease);
            out[j] = mul28(x, scale);
        }
    }
    voice.peak = peak;
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = soft_limit(out[i]) * kLevel;
}
}  // namespace chimes
