# Cirrus

A granular sample instrument after Mutable Instruments Clouds, by Emilie
Gillet, rebuilt in fixed point for the OP-XY. Load a sample on the stock
sampler page; each note then plays it as Clouds plays its recording: in grains,
scattered, pitched and spread across the stereo field, at the note's pitch.
Where Clouds' recording moves on with the input, Cirrus' grains move through
the sample at Speed. The [licence](vendor/LICENSE) and
[provenance](vendor/README.md) of the upstream code remain with the port.

It is Clouds' granular player at its 16-bit quality: in stereo for a stereo
sample, in mono for a mono one. Clouds' feedback, diffuser and reverb belong to
an effect: send the track to [Cumulus](../cumulus) for them.

Four controls, and three more on Shift:

| Control | Clouds' | Does |
| --- | --- | --- |
| **Position** | POSITION | Where in the sample the grains start: 0 at its start, 99 at its end |
| **Size** | SIZE | Each grain's length, 32 ms to 0.5 s |
| **Density** | DENSITY | Grains at regular intervals up to 46, none from 47 to 52 besides the note's own, at random from 53; more of them further from the middle |
| **Texture** | TEXTURE | The grain window, hard-edged at 0 to smooth at 75 and above |
| **Pitch** | PITCH | Transposes the grains from the note, lingering at Clouds' notches: -24 at 0, -12 at 7, -7 at 13, 0 from 46 to 53, +7 at 86, +12 at 92, +24 at 99 |
| **Speed** | (the recording) | Moves the grains through the sample: still at 50, forward at the sample's own speed at 99, backward at 1. A note starts again from Position |
| **Spread** | BLEND (spread) | Pans each grain at random about the centre, by up to the whole field at 99 |

Positions are 99ths of the control's travel. Presets: Cloud, Stretch,
Stutter, Octave Down, Frozen and Rewind.

## Playing it

- **Pitch.** Middle C plays the sample at its own pitch and speed, as the
  stock sampler does; the pad's tuning, the sample's rate, the octave buttons
  and the track's transpose move it as they move the stock sampler. Pitch
  transposes the grains on top. A grain keeps the pitch it started with.
- **A note starts a grain at once**, as Clouds' trigger input does, and plays
  from Position again. At Density 50 that grain is all a note plays.
- **The sample's trim** bounds the grains: they start, and end, inside it.
  Its reverse setting is not used; Speed below 50 plays through it backward.
  A trim shorter than 128 frames plays nothing.
- **Level.** The pad's level scales the output. A sample at full scale plays
  at most at half the OP-XY's full scale, Clouds' own full scale.
- **Each note scatters its grains its own way**, so the notes of a chord do
  not move together.
- New presets have no attack, full sustain and a long release.

```sh
python ../open-xy/sdk.py test --machine machines/cirrus
```

Sample engines have no browser preview; the [reference kit](../../tools/README.md#comparing-a-port-with-the-original)
is how the sound is checked against Clouds.

| File | Owns |
| --- | --- |
| [definition.json](definition.json), [machine.json](machine.json) | Controls, presets, stereo output, SDK version and identity |
| [core.h](core.h) | A voice's state: its sample, its grains and their scheduler |
| [granular.h](granular.h) | Clouds' grains: the controls, scheduling, the envelope, reading the sample and the normalization |
| [sample.cpp](sample.cpp) | The callbacks: a voice for a sample, a note, a block of output |
| [fixed.h](fixed.h), [tables.h](tables.h) | Fixed-point arithmetic and Clouds' lookup tables |
| [tests/sound.cpp](tests/sound.cpp) | Sound checks: silence without a sample or room for a grain, pitch by note, tuning, sample rate and Pitch, the note's grain, Position, Speed forward, still and back, Spread for mono and stereo samples, each note's own scattering, noise keeping its spectrum, the pad's level and half scale, silence without an offset |

## Port fidelity

All DSP is 32-bit integer: audio is Q26 (Clouds' full scale is 2^26), the
sample 16-bit as Clouds' recording, gains Q28 or Q31, and Clouds'
exponentials, windows and soft limiter come from tables. Clouds' 32 kHz time
constants are rescaled to 44.1 kHz, so grain sizes keep their times in
seconds.

Against upstream Clouds fed a steady tone, with Cirrus playing a sample of
the same tone at Speed 99, so that both read grains of the tone as it flows
(`tools/reference/clouds/cirrus.py`):

- **Pitch** inside the grains is within a cent at every notch, where Clouds' own notches are up to 4 cents out.
- **Level** matches within 4% at Density 30, and is 0.94 times Clouds' at 65 over 16 runs, where a few random grains make any run vary. At 80 it is 0.92 to 0.99 times, and at 90 about 0.7 (stereo) and 0.65 (mono): see the grain limit below. At 40, a grain every few seconds, a measurement holds one grain or none.
- **Spectrum.** Noise through the grains keeps its spectrum within 0.2 dB, as through Clouds': a grain starts where the scan is at its own sample, so overlapping grains read in step.
- **Spread** widens the field as Clouds' does where grains are sparse: within 4% for a mono sample and 10% for a stereo one. Denser, fewer grains average fewer random pans, and the field is up to a fifth wider than Clouds'.

Deliberate differences:

- **A sample in place of the recording.** The grains read the sample and nothing records: no Freeze, which Speed 50 replaces, and no feedback, diffuser or reverb, which need a recording or a long memory for every note (Cumulus has them).
- **Speed.** Clouds' grains follow its recording head as it moves on with the input. Cirrus' follow a scan through the sample, at Speed; at 99 it moves as the head does. Position measures forward from the scan, where Clouds' measures back from the head.
- **Up to 6 grains a note**, the first 2 at high quality. Clouds plays up to 32 in stereo and 40 in mono, at high quality while fewer than a quarter play. Density aims for 32 overlap³ grains in stereo and 40 in mono, which passes 6 below 20 and above 79 (22 and 77 in mono): there Cirrus is less dense than Clouds, and quieter.
- **A note's own grain**, as Clouds' trigger input seeds one.
- **Pitch's notches on whole values**, the stepped envelope and per-block modulation, as in Cumulus.
- **Grains read at most 32 times the sample's speed**, five octaves up; a grain is shortened to fit the trim at its speed.
- **Not ported.** Dry/wet, the trigger and freeze inputs, the diffuser and reverb (a send's), and Clouds' other modes.

## Native notes

Each of the 256 voices the stock sampler creates holds its sample's view, its
6 grains and their scheduler: 332 bytes, and 110.5 KiB in the module with the
SDK's own voice slots. The sample is read where stock keeps it.

From the pinned compiler's Blackfin output, roughly:

| Stage | Instructions per 64-sample block |
| --- | --- |
| A grain of a mono sample, high quality (Hermite) | About 4,000 |
| A grain of a mono sample, medium quality (linear) | About 2,400 |
| A grain of a stereo sample, high quality | About 6,100 |
| A grain of a stereo sample, medium quality | About 3,400 |
| Normalization, the soft limit and output | About 4,200, and scheduling a few hundred more |

So a note at the defaults (Density 80, about 4.5 grains) needs about 15,000
for a mono sample and 20,000 for a stereo one, and at its densest about 18,000
and 24,000. Each note is a voice: chords add up. These are instructions, not
cycles, from counting the compiled loops. On an OP-XY, the SDK's hardware
benchmark (`opxy-sample-callback-v1`: one note of a two-second mono sample at
the defaults) measured 4.6% of the block, the median of three runs.

## On the device, still to check

- How stock sample page 2 shows Cirrus' controls, and what its trim markers do.
- The CPU of a chord, against the stock sampler's voices.
- Sample engines draw no screen of their own. The grains are to show once the
  SDK lets a sample engine draw over the stock page.
