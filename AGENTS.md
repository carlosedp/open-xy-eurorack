# Instructions for agents

This repository holds Open-XY machines after Emilie Gillet's Eurorack modules
(Rings so far; Clouds and Beads planned). It is not the SDK.

- Clone the SDK beside this repository (`git clone
  https://github.com/etcetc000/open-xy ../open-xy`, default branch `staging`) and
  run its commands from here, for example
  `python ../open-xy/sdk.py test --machine machines/<id>`. Read the SDK's
  machine API and the core machines (Macro Osc for a Mutable port, Karplus and
  Ping Pong for strings and effects) before changing a machine.
- Change a machine only inside its own folder. Machine code uses only the public
  `<opxy/...>` headers: no assembly, platform conditionals, OS headers or raw
  addresses, and audio callbacks never allocate, block, log or do I/O. SDK gaps
  are separate pull requests to the SDK's `staging` branch.
- No floating point and no 64-bit arithmetic in machine code: the Blackfin has
  no FPU and the firmware links no helpers for either. Use each machine's
  `fixed.h`. Floats are fine in `tests/`.
- Keep upstream notices with the code derived from them (`vendor/LICENSE`), and
  say in `vendor/README.md` which upstream code each file follows. Table and
  artwork generators live in `tools/`, never in a machine folder; regenerate
  rather than hand-edit their output.
- Check a port against the upstream float code with the kit in
  [tools/reference/](tools/README.md#comparing-a-port-with-the-original): fetch,
  build, compare. A new module gets its own folder there. Compare noise-excited
  sounds as averages over several strikes.
- Host CPU estimates overstate `q31::multiply`-heavy code; count the pinned
  compiler's output with `tools/blackfin_loops.py` instead, and never present
  either as a hardware measurement.
- A release is a tag here, then a pull request against the SDK's `staging` that
  changes only `catalog/machines.json`.
- Device operations need the user's explicit instruction each time.

## Conventions the user has set

- **Names:** machines are not named "Mutable Instruments" or after the
  module (a registered trademark). They take a name of their own (Rings became
  Chimes) and credit Emilie Gillet and the original in their README.
- **One repository** holds all the ports, one folder each under `machines/`.
- **Controls read 0–99, as on the stock engines.** A synth's display replaces
  the SDK's one-decimal captions with the control's travel × 99, rounded (see
  `caption` in Chimes' [display.cpp](machines/chimes/display.cpp)). Put a
  control's landmarks on round values: Chimes' Struct 25 is harmonic, Position
  50 the middle, Damping 99 endless. Defaults and presets sit on n / 99. Effects
  keep stock FX captions; the SDK has no override for them.
- **Fidelity first, but in tune.** Follow upstream formula for formula and
  check it with the reference kit. Where upstream is audibly wrong for a keyboard
  instrument (Rings' strings run up to 25 cents sharp at high notes), fix it and
  document the deviation in the machine's README.
- **Screens are simple.** No decorative elements beyond the instrument itself:
  the user removed Chimes' mallet and strike glows. Keep to what the controls show.

## Lessons from Chimes

- **Fit the CPU budget by choosing upstream's own smaller configuration**
  rather than inventing one. Chimes is a Rings voice at polyphony 4 (12 modes, 2 strings).
- **Rounding in recursive filters must carry its remainder** (Chimes' `DcBlocker`).
  Without it, the floor rounding of `q31::multiply` adds a constant offset that a
  feedback loop amplifies.
- **Store audio that feeds back at 32 bits**, not 16. A slowly decaying loop
  otherwise sticks at a constant value.
- **Keep audio for the OP-XY's output mono and DC-free.** Mirror how upstream
  sends a voice to its outputs, and AC-couple the output.
- **The SDK's validator rejects any text slot** (`tooling/native.py` treats
  `node.keys()` as a set). Fixing that is a separate SDK pull request.

## Next: Clouds, then Beads

- **Kind:** send effects, with `bufferBytes` up to 512 KiB of the send slot's
  memory; `State` at most 472 bytes.
- **Budget:** about 30,000 instructions per 64-sample block, once per track.
  There is no browser preview for effects, so the reference kit is how the sound is checked.
- **Sample rate:** Clouds runs at 32 kHz and Beads at 48 kHz. Rescale their time
  constants to 44.1 kHz, as Chimes rescaled Rings' 48 kHz ones.
- **Controls:** at most 8. Dry belongs to the stock mixer (`dryParameter`).
  Freeze and grain triggers become controls, and timing can follow the project tempo
  (`resources.transport`).
- **Clouds' spectral mode** (an FFT phase vocoder) is likely too heavy; leave it out at first.
- **Beads' 4 s stereo buffer** at 44.1 kHz needs about 690 KiB, so plan for about
  3 s stereo or 4 s mono.
