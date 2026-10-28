# Stratus

A granular send effect after Mutable Instruments Clouds, by Emilie Gillet,
rebuilt in fixed point for the OP-XY. Whatever the tracks send to it, Stratus
records into Clouds' buffer and plays back as grains: scattered, pitched,
frozen and through Clouds' reverb. The
[licence](vendor/LICENSE) and [provenance](vendor/README.md) of the upstream
code remain with the port.

It is Clouds in its granular mode at its 16-bit stereo quality: a 1.02 s
stereo recording, each channel's grains playing its own. The return is all
wet; each track keeps its dry sound in the stock mixer, and its send level sets
how much reaches Stratus.

Clouds' four main controls, and on a press of the same encoder its other four:

| Encoder | Control | Press for | Clouds' | Does |
| --- | --- | --- | --- | --- |
| 1 | **Position** | | POSITION | How far back in the recording grains start: 0 the most recent, 99 a second ago |
| | | **Freeze** | FREEZE | Stops recording: the grains play what the buffer holds |
| 2 | **Size** | | SIZE | Each grain's length, 32 ms to 0.5 s |
| | | **Texture** | TEXTURE | The grain window, hard-edged at 0 to smooth at 75, then the diffuser blurs them toward 99 |
| 3 | **Pitch** | | PITCH | Transposes the grains, lingering at Clouds' notches: -24 at 0, -12 at 7, -7 at 13, 0 from 46 to 53, +7 at 86, +12 at 92, +24 at 99 |
| | | **Dry** | BLEND (dry/wet) | How much each track's send turns down its dry sound: at 0 a track's send is its dry/wet, fully wet at 100%; at 99 sends leave the dry as it is. The stock mixer's, through `dryParameter` |
| 4 | **Density** | | DENSITY | Grains at regular intervals up to 46, none from 47 to 52, at random from 53; more of them further from the middle |
| | | **Reverb** | BLEND (reverb) | Clouds' reverb on the return |

Pressing an encoder again returns it to its main control. The alternates need
an SDK whose send pages carry an effect's controls 5 to 8 (PR A); on one
without, they read 0: hard-edged grains, not frozen, no feedback or reverb.

Values read 0–99, the stock FX captions; Freeze reads off or on. Presets: Cloud,
Shimmer, Stutter, Octave Down, Wash and Crackle.

```sh
python ../open-xy/sdk.py test --machine machines/stratus
```

Effects have no browser preview; the [reference kit](../../tools/README.md#comparing-a-port-with-the-original)
is how the sound is checked against Clouds.

| File | Owns |
| --- | --- |
| [definition.json](definition.json), [machine.json](machine.json) | Controls, presets, the send buffer, SDK version and identity |
| [core.h](core.h) | The effect's state and Clouds' state in the send buffer |
| [clouds.h](clouds.h) | Clouds' recording, grains, feedback, diffuser and reverb |
| [fx.cpp](fx.cpp) | The callbacks: clearing the buffer, the send in and the return out |
| [fixed.h](fixed.h), [tables.h](tables.h) | Fixed-point arithmetic and Clouds' lookup tables |
| [tests/sound.cpp](tests/sound.cpp) | Sound checks: the controls playing Clouds' knobs, silence outside a send and while clearing, an old buffer's contents never heard, silence in and out with no offset left, grains in tune, Density's dead zone, the channels kept apart, a quiet send's detail, and at Clouds' other knobs Freeze, Feedback dying away and staying in range, Reverb's tail, an engine reset |

## Port fidelity

All DSP is 32-bit integer: audio is Q26 (Clouds' full scale is 2^26), the
recording Q26 at 32 bits (see below), gains Q28 or Q31, and Clouds' exponentials,
windows and soft limiter come from tables. Clouds' 32 kHz time constants and
delay lengths are rescaled to 44.1 kHz, so grain sizes, positions and the
reverb keep their times in seconds. The send's full scale is Clouds' input
full scale, and the return is its 16-bit output at full scale.

Against upstream Clouds, built on the host and fed the same tone or burst
(`tools/reference/clouds/compare.py`):

- **Level** through the grains matches within 4% at Density 30, 40, 80 and 90 over Size and Texture, and within 20% at 65, where a few random grains make any 1.5 s vary. Randomly seeded settings are averaged over several runs. Like Clouds, Stratus plays up to 32 grains at once, the first quarter of them at Clouds' high quality.
- **Grain intervals** below Density's middle match within 2%.
- **Pitch** inside the grains is within 2 cents at every notch.
- **Position's delay** matches within 2 ms from 0 to 75.
- **Freeze** holds the same recording; **Feedback** and **Reverb** tails match within the spread of single bursts (about ±15%).

Deliberate differences:

- **Pitch's notches on whole values.** Clouds spaces its notches a fifteenth of the knob apart, so most fall between two of the OP-XY's 0–99 values and +12 would play at +12.4. Each notch falls on the nearest whole value here, with Clouds' crossfade between them.
- **The envelope in steps.** A grain's window is computed exactly every 8 samples and interpolated between; over Clouds' shortest grain, 1411 samples, it departs from the window by less than 1e-4.
- **Finer storage.** The recording keeps 32 bits where Clouds' keeps 16, within the same limits: an OP-XY send arrives far below Clouds' full scale, where 16 bits would leave a few steps of each note, a burst of noise in place of the grains. The reverb and diffuser keep 32 bits where Clouds' reverb keeps 12, and the return keeps 32 where Clouds' output keeps 16. Tails decay as Clouds' do, below where Clouds' fall to zero.
- **Modulation per block.** The reverb's slow modulation (0.5 and 0.3 Hz) steps at each 32-sample block, where Clouds steps it every 32 samples.
- **The return 12 dB louder.** Clouds plays its dry at 0.707 of the input and its wet at 0.707 × 1.2 of the grains, then halves both, so its wet sits 1.2 times above its dry. The OP-XY's mixer plays the dry as it is, so raising Clouds' wet by 2√2 (9 dB) would sit it against that dry as Clouds' wet sits against its own. Against a full mix on the OP-XY that still sounded low, so Stratus raises it by 4 (12 dB), 3 dB above Clouds' balance, saturating at full scale. Tests and the reference kit compare Clouds' wet itself (`render`).
- **Feedback without a control.** Clouds' feedback is ported and tested, but its encoder press is Dry: a send effect has eight controls, and Dry lets the grains be heard without the dry sound over them.
- **Not ported.** Stereo spread (grains stay centred, keeping the send's own stereo), the trigger input, and Clouds' other modes: Stretch, Looping Delay and the spectral mode.

## Native notes

Clouds lives in the send's buffer: the recording, the grains, the feedback
filters, the diffuser and the reverb, 464,788 bytes. The buffer holds whatever
the slot's last effect left, so Stratus clears it 16 KiB a block, silent for
its first 29 blocks (42 ms) after it is placed or the engine resets. Outside a
send it is silent.

Each delay line of the reverb and diffuser is a ring of its own, so that a
line's oldest word is also the one written next, and the reverb runs as three
passes over each block: its input diffusion, then each side of its loop.
Neither changes the output.

From the pinned compiler's Blackfin output, roughly:

| Stage | Instructions per 64-sample block |
| --- | --- |
| A grain at Pitch 0 (50) | About 2,200 |
| A transposed grain, high quality (Hermite, the first 8) | About 6,200 |
| A transposed grain, medium quality (linear) | About 3,400 |
| The reverb, when Reverb or a frozen Feedback is up | About 19,000 |
| The diffuser, Texture above 75 | About 5,500 |
| Recording, scheduling, normalization and output | About 5,000, and 1,800 more with Feedback |

So the defaults (about 6 grains at Pitch 0, no reverb) need about 18,000, and
the densest, Density 99 with every grain transposed, about 135,000 without
reverb. These are instructions, not cycles, from counting the compiled loops.
On an OP-XY, the SDK's hardware benchmark (`opxy-effect-callback-v1`, at the
defaults) measured 5.7% of the block, the median of three runs.
