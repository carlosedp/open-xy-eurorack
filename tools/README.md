# Tools

Generators, and the kit for checking a port against the module it follows.
Nothing here is built into firmware. Downloads and builds go to the ignored
`local/`. Commands run from the repository root, with the
[SDK](https://github.com/etcetc000/open-xy) checked out beside it as `../open-xy`.

| Tool | Does |
| --- | --- |
| [chimes_tables.py](chimes_tables.py), [chimes_panel.py](chimes_panel.py) | Generate Chimes' `tables.h` and `panel.svg.fragment`; CI checks they are current |
| [clouds_tables.py](clouds_tables.py) | Generates the `tables.h` of the machines after Clouds, Stratus and Cirrus; CI checks they are current |
| [reference/](reference) | Builds the upstream float code and the machine on the host, and compares them |
| [blackfin_loops.py](blackfin_loops.py) | Counts the Blackfin instructions in a machine's loops (synth, effect or sample engine), from the pinned compiler |
| [host/sdk_cxx14.py](host/sdk_cxx14.py) | `sdk.py test` with C++14 host compiles, for clang++ on Windows |

## Comparing a port with the original

```sh
python tools/reference/upstream.py           # pinned eurorack and stmlib, into local/upstream/
python tools/reference/build.py rings        # local/reference/rings/reference and probe
python tools/reference/rings/compare.py grid
python tools/reference/rings/compare.py tuning
python tools/reference/rings/compare.py envelope 2 110 0.25 0.3 0.4 0.3
python tools/reference/build.py clouds       # Clouds against Stratus and Cirrus
python tools/reference/clouds/compare.py all # Stratus
python tools/reference/clouds/cirrus.py all  # Cirrus
```

`build.py` takes `--cxx` (a C++14 compiler, `clang++` by default) and `--sdk`.

Each module has a folder in [reference/](reference) holding:

- `reference.cpp`: the upstream code driven on the host at its own sample rate.
- `probe.cpp`: the machine, through its `engine.cpp` or `fx.cpp`.
- `build.json`: the upstream sources and the machine folder; under `probes`,
  any other machine after the module, each with its own harness (Clouds':
  `cirrus.cpp`, which plays Cirrus' `sample.cpp`).
- `compare.py`: the module's checks, built on [analysis.py](reference/analysis.py),
  and a script of checks for each further probe (Clouds': `cirrus.py`).

The reference and its probe take the same arguments and end with an output file of
32-bit floats and an optional noise seed; a further probe takes its machine's
controls instead, and the reference takes optional settings after the seed
(Clouds': stereo spread and quality). Both write in the module's own units
(Rings' full scale is 1.0 on both sides), so levels compare directly.

`analysis.py` measures in seconds and hertz, because the two sides run at
different rates. It has envelopes, partial and harmonic levels, band energies,
DFT-peak tuning (autocorrelation mixes in a string's upper partials) and a
rate-scaled brightness. `averaged` takes the mean over strikes: a sound struck
by noise differs from strike to strike, so compare those as averages over
several seeds, never one strike.

### Adding Beads

Follow [clouds/](reference/clouds), an effect's kit:

1. Add the module's folders to `upstream.py`'s sparse checkout if they are not there.
2. Make `tools/reference/beads/`: `build.json` with Beads' DSP sources, a `reference.cpp` running it at 48 kHz, and a `probe.cpp` running the effect's `process` at 44.1 kHz.
3. An effect needs input: both harnesses read an input file of floats at their own rates, which `compare.py` renders from the same signal function at each rate, and write the stereo output interleaved.
4. Write `compare.py` with the module's checks. Clouds' measures level over the controls, grain intervals, pitch inside the grains (zero crossings: a spectrum splits restarted grains into lines), the delay of a burst, Freeze, and the feedback and reverb tails.

## CPU

`sdk.py test` prints a host-timed estimate. It overstates code built on
`opxy::q31::multiply`, which is a 64-bit emulation on the host and one
instruction on the Blackfin: Chimes reads about 88,000 against about
13,000–19,000 from its compiled code.
`python tools/blackfin_loops.py machines/<id>` lists each loop's instructions
per iteration (fetch the pinned tools first with
`python ../open-xy/sdk.py toolchain fetch`). Neither is a hardware measurement;
the catalog requires the SDK's [hardware profiling](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md).
