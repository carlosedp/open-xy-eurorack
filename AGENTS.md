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
