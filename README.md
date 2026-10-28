# Open-XY Eurorack ports

Machines for the OP-XY on OS 1.2.0, after Emilie Gillet's open-source Eurorack
modules, built with the [Open-XY SDK](https://github.com/etcetc000/open-xy).
They are fixed-point reimplementations: the modules compute in floating point,
which the OP-XY's Blackfin does not have.

| Machine | Kind | After |
| --- | --- | --- |
| [Chimes](machines/chimes) | Synth | Rings: modal resonator, sympathetic, inharmonic and chorded strings |
| [Stratus](machines/stratus) | Send effect | Clouds: granular recording with Freeze and reverb |
| [Cirrus](machines/cirrus) | Sample instrument | Clouds: a sample played as grains from the keyboard, scanned at its own speed |

Beads is planned as a send effect.

"Mutable Instruments" is a registered trademark. These machines are not
affiliated with or endorsed by Mutable Instruments, and the name is used here only
to credit the originals.

Each folder is one machine: `machine.json` (kind, SDK version and the numbers it
prefers), `definition.json` (controls), its C++, artwork and sound tests in
`tests/`. [tools/](tools) holds what the machine folders may not hold: the
table and artwork generators, the kit that builds each module's upstream code on
the host and compares a port with it, and a Blackfin instruction counter.

## Checks

Clone the SDK next to this repository, then from here:

```sh
python ../open-xy/sdk.py test --machine machines/chimes --machine machines/stratus --machine machines/cirrus
python ../open-xy/sdk.py preview --machine machines/chimes --out ../previews/chimes-001
python ../open-xy/tools/serve_harness.py --preview ../previews/chimes-001
```

CI checks every machine and compiles the firmware module with all of them on
each push. Hardware CPU measurement and catalog submission follow the SDK's
[profiling](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md)
and [catalog](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine)
guides.

## Licences

MIT ([LICENSE](LICENSE)). Each machine derived from Mutable Instruments code
carries that code's MIT notice in its `vendor/LICENSE`, for example
[Chimes'](machines/chimes/vendor/LICENSE).
