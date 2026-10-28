# Chimes

A resonator after Mutable Instruments Rings, by Emilie Gillet, rebuilt in
fixed point for the OP-XY. Each note strikes or plucks it with Rings' internal
exciter, and the resonance is the note: the OP-XY's envelope and level follow.
The [licence](vendor/LICENSE) and [provenance](vendor/README.md) of the
upstream code remain with the port.

Each voice is one of Rings' four voices at polyphony 4, the setting Rings
itself plays chords with, and the OP-XY supplies the polyphony.

| Model | Sound |
| --- | --- |
| **modal** | 12 band-pass partials: bars, bells, plates and glass |
| **sympathetic** | A plucked string and a second string tuned by Structure that rings by sympathy |
| **string** | A plucked string with Rings' dispersion: stiff and bell-like above Structure 0.26, buzzing on a curved bridge below 0.24, noisier toward 1 |
| **chord** | The sympathetic strings tuned to one of Rings' eleven intervals, chosen by Structure |

Four encoders: **Struct**, **Bright**, **Damping** and **Position**, with
Rings' meanings, and a second page with **Model** under Struct. As on Macro
Osc, the header shows both pages, the one the encoders edit large and bright:
pressing any encoder switches pages. Bright, Damping and Position repeat on
the second page.

- **Struct** stretches the partials (modal), tunes the sympathetic string (sympathetic), sets dispersion (string), or picks the interval (chord).
- **Bright** opens the exciter and the strings' damping filters.
- **Damping** sets the decay, to an endless ring at the top of its range.
- **Position** sets where the resonator is struck and heard.

Values read 0–99, as on the stock engines: a control's travel × 99, rounded.
Each value n recalls exactly as n / 99, and Shift + turn moves between the
steps. The landmarks fall on round numbers:

- **Struct 25** is harmonic in every model: Rings' stiffness and dispersion are zero there.
- **Position 50** is the middle.
- **Damping 99** rings without end.
- **In the chord model**, Struct's eleven intervals centre on 0, 10, 20 … 90 and 99.

The presets and defaults sit on whole values.

The screen shows twelve chimes hanging on cords from a rail, one for each
modal partial:

- **Struct** sets their lengths, as a chime's pitch rises with the square of its shortening.
- **Position** sets how far a note swings each chime: the weight the sound gives its partial.
- **Damping** sets how long the chimes swing.
- **Bright** dims the shortest chimes.
- The header names the model, and in the chord model Struct's interval.

Presets: Bell, Handpan (nearly harmonic modal partials, slightly stretched
toward metal, with a long ring), Marimba, Glocken (stiff, bright bars, as a
glockenspiel's), Sympathetic, Pluck, Twang and Chord.

```sh
python ../open-xy/sdk.py test --machine machines/chimes
python ../open-xy/sdk.py preview --machine machines/chimes --out ../previews/chimes-001
python ../open-xy/tools/serve_harness.py --preview ../previews/chimes-001
```

| File | Owns |
| --- | --- |
| [definition.json](definition.json), [machine.json](machine.json) | Controls, presets, resources, SDK version and identity |
| [core.h](core.h), [engine.cpp](engine.cpp) | Voice and shared state, lifecycle, the voice's output and limiter |
| [modal.h](modal.h) | The modal resonator and its strike |
| [strings.h](strings.h) | The strings, their plucker, sympathetic tunings and chords |
| [fixed.h](fixed.h), [tables.h](tables.h) | Fixed-point arithmetic and Rings' lookup tables |
| [display.h](display.h), [display.cpp](display.cpp), [panel.svg.fragment](panel.svg.fragment) | The screen |
| [tests/sound.cpp](tests/sound.cpp) | Sound checks: silence before a note, the declared peak, no DC, fading, tuning within 5 cents from 41 Hz to 2 kHz, Damping and Brightness, lease stealing and model changes |

## Port fidelity

All DSP is 32-bit integer: audio is Q26 (Rings' full scale is 2^26),
coefficients Q28 or Q31, and Rings' exponentials, tangents, reciprocals and
soft limiter come from tables. Each trapezoidal filter keeps Rings' structure,
with its coefficients arranged so that rounding cannot make a resonance grow.
Rings' 48 kHz time constants are rescaled to 44.1 kHz, so decays and filters
keep their times in seconds.

Against upstream Rings, built on the host at polyphony 4 and struck the same way:

- **Modal** matches its level, its decay at 0.3 s and 1 s, and its brightness, within a few percent.
- **The strings** match in decay rate and in their harmonics within a few dB, and in level within about ±3 dB. Both are struck by noise, so these are averages over many strikes.

Deliberate differences:

- **Polyphony 4 per voice.** Rings at polyphony 1 runs 60 partials and 8 strings, about eight times an OP-XY voice's budget.
- **The plain strings' fractional delay.** Rings reads with Hermite interpolation each sample; here a first-order all-pass supplies the last 1 to 2 samples, retuned once per block, as Karplus does. The magnitude stays flat, and the measured difference from Hermite is small. The string model's noisy and curved-bridge ranges, whose delay moves every sample, read with linear interpolation.
- **Tuning.** Rings compensates its strings' damping filter by the cutoff's ratio to the note, which ignores the filter's frequency warping. Upstream measures +13 cents at 1 kHz, +25 at 2 kHz and -11 at 3.5 kHz, depending on Brightness. Chimes compensates by the filter's actual phase and stays within 3 cents. In-tune strings resonate together more strongly, so at high Brightness and Damping on high notes the sympathetic and chord models ring louder than Rings, by up to about 10 dB.
- **No V/OCT.** The keys give the note, and Rings' FREQUENCY knob (the tonic of the sympathetic tunings) is the note itself, as on Rings with nothing patched to V/OCT.
- **Not ported.** Rings' FM voice, the stereo string-and-reverb model, the external audio input, its note filter, and its upsampler for strings below 21.5 Hz, where Chimes plays its lowest string instead.
- **Rate of change.** Position's partial weights and the pickup's position change once per 64-sample block.

## Native notes

Each note sets `strum`; the next block excites the resonator. A modal strike
injects Rings' pulse into its low-pass as the filter's response to it, since the
pulse itself, `0.25 x 2^(2 fc^2) / fc`, overflows fixed point.

The string models lease two Q26 lines of 2,048 samples and a 256-sample pluck
comb (17,408 bytes) from a pool of 12. When the pool is full the oldest lease
is stolen and that voice falls silent until struck again. The modal model
needs no lease.

Measured on an OP-XY with the SDK's hardware benchmark (`opxy-synth-c8-v1`:
note 60, the four main controls at their middle, the modal model), as a share
of the 580,352-cycle block, median of three runs:

| Part | Share of the block |
| --- | --- |
| A whole voice, with stock's filter and envelope | About 4.5% |
| Each of the 12 partials | About 0.19% |
| The output's AC coupling and limiter | About 0.64% |
| Stock's filter and envelope alone | About 0.80% |

The partials run in a loop of their own, which the compiler makes a Blackfin
hardware loop; inlined into the loop over partials it branches each sample.
The coupling, the limiter's gain and the soft limit are three passes over the
block, each within the Blackfin's registers. Rings retunes its partials every
block; here a control's move retunes them all, and a note's move retunes a
quarter of them a block, in turn, which catches up within four blocks. The
strike's filter is set up only while it rings. None of this changes the
output. Fewer partials save about 0.19% each, at an audible cost: Chimes keeps
Rings' twelve.

`sdk.py test`'s host-timed estimate reads several times higher: on the host,
`q31::multiply`, which Chimes uses for nearly every product, is a 64-bit
multiply with a rounding correction, and on the Blackfin it is one instruction.
