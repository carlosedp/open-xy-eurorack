# Chimes against upstream Rings. From the repository root, after
# tools/reference/upstream.py and tools/reference/build.py rings:
#   python tools/reference/rings/compare.py grid [--strikes 4] [--quick]
#   python tools/reference/rings/compare.py envelope|harmonics|bands <model> <hz> <structure> <brightness> <damping> <position>
#   python tools/reference/rings/compare.py tuning [--model 2]
# Models are Chimes': 0 modal, 1 sympathetic, 2 string, 3 chord. Levels are in
# Rings' units (1.0 is Rings' full scale) on both sides; noise-excited models are
# averaged over --strikes strikes.
import argparse
import itertools
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import analysis  # noqa: E402

NAMES = ['modal', 'sympathetic', 'string', 'chord']
RINGS_MODEL = {0: 0, 1: 1, 2: 2, 3: 4}  # chord is Rings' quantized sympathetic model


def sides():
    return analysis.Harness('rings', 'reference', 48000), analysis.Harness('rings', 'probe', 44100)


def render(harness, model, settings, seconds, seed):
    m = RINGS_MODEL[model] if harness.rate == 48000 else model
    return harness.render(m, *settings, seconds, seed=seed)


def summary(x, rate):
    start = analysis.rms(x, rate, 0, 0.05)
    return np.array([analysis.rms(x, rate, 0, 1), analysis.rms(x, rate, 0.28, 0.32) / start,
                     analysis.rms(x, rate, 0.95, 1.05) / start, analysis.brightness(x, rate)])


def grid(args):
    reference, probe = sides()
    hz_values = (110, 440, 1760)
    space = itertools.product(range(4), hz_values, (0.25, 0.6, 0.9), (0.3, 0.7), (0.4, 0.8))
    if args.quick:
        space = itertools.product(range(4), (220,), (0.25, 0.6), (0.5,), (0.6,))
    ratios = {m: [] for m in range(4)}
    for model, hz, structure, bright, damping in space:
        settings = (hz, structure, bright, damping, 0.3)
        strikes = 1 if model == 0 else args.strikes  # modal is struck by a pulse: no noise
        r, c = (analysis.averaged(lambda seed: summary(render(h, model, settings, 1.1, seed), h.rate),
                                  analysis.seeds(strikes)) for h in (reference, probe))
        ratios[model].append(c[0] / max(r[0], 1e-9))
        print(f'{NAMES[model]:<11} {hz:>5} s{structure:<4} b{bright:<3} d{damping:<3} | '
              f'rms {r[0]:.4f} {c[0]:.4f} ({c[0] / max(r[0], 1e-9):5.2f}x) | @0.3 {r[1]:.3f} {c[1]:.3f} | '
              f'@1 {r[2]:.3f} {c[2]:.3f} | bright {r[3]:.4f} {c[3]:.4f}')
    for model in range(4):
        values = sorted(ratios[model])
        print(f'{NAMES[model]}: Chimes / Rings RMS median {values[len(values) // 2]:.2f}, '
              f'range {values[0]:.2f}..{values[-1]:.2f}')


def setting(args):
    return args.model, (args.hz, args.structure, args.brightness, args.damping, args.position)


def envelope(args):
    model, settings = setting(args)
    curves = [analysis.averaged(lambda seed: analysis.envelope(render(h, model, settings, 1.1, seed), h.rate),
                                analysis.seeds(args.strikes)) for h in sides()]
    print('t (s)  rings    chimes   ratio')
    for i, (a, b) in enumerate(zip(*curves)):
        print(f'{i * 0.05:4.2f}  {a:.5f}  {b:.5f}  {b / max(a, 1e-12):5.2f}')


def harmonics(args):
    model, settings = setting(args)
    for name, h in zip(('rings', 'chimes'), sides()):
        magnitudes = analysis.averaged(
            lambda seed: analysis.levels(render(h, model, settings, 1.1, seed), h.rate,
                                         [k * args.hz for k in range(1, 9)]), analysis.seeds(args.strikes))
        print(f'{name:<7} harmonics 1..8, dB re the fundamental:',
              np.round(20 * np.log10(magnitudes / magnitudes[0] + 1e-12), 1))


def bands(args):
    model, settings = setting(args)
    edges = tuple(20 * 2 ** k for k in range(11))
    powers = [analysis.averaged(lambda seed: 10 ** (analysis.bands(render(h, model, settings, 1.1, seed), h.rate,
                                                                    edges) / 20), analysis.seeds(args.strikes))
              for h in sides()]
    for a, b, r, c in zip(edges, edges[1:], *powers):
        print(f'{a:6d}-{b:6d} Hz: rings {20 * np.log10(r):7.1f} dB  chimes {20 * np.log10(c):7.1f} dB')


def tuning(args):
    reference, probe = sides()
    for bright, damping in ((0.5, 0.6), (0.9, 0.8)):
        for hz in (110.0, 523.25, 1046.5, 2093.0, 3520.0):
            settings = (hz, 0.25, bright, damping, 0.3)
            r, c = (analysis.fundamental_cents(render(h, args.model, settings, 0.4, 777), h.rate, hz)
                    for h in (reference, probe))
            print(f'{NAMES[args.model]} brightness {bright} damping {damping} {hz:7.1f} Hz: '
                  f'rings {r:+6.2f} cents, chimes {c:+6.2f} cents')


def main():
    parser = argparse.ArgumentParser(description='Chimes against upstream Rings')
    commands = parser.add_subparsers(dest='command', required=True)
    g = commands.add_parser('grid', help='level, decay and brightness over a grid of settings')
    g.add_argument('--strikes', type=int, default=4)
    g.add_argument('--quick', action='store_true', help='a handful of settings')
    g.set_defaults(run=grid)
    for name, run in (('envelope', envelope), ('harmonics', harmonics), ('bands', bands)):
        p = commands.add_parser(name)
        for field, kind in (('model', int), ('hz', float), ('structure', float), ('brightness', float),
                            ('damping', float), ('position', float)):
            p.add_argument(field, type=kind)
        p.add_argument('--strikes', type=int, default=16)
        p.set_defaults(run=run)
    t = commands.add_parser('tuning', help='the fundamental in cents, by note')
    t.add_argument('--model', type=int, default=2)
    t.set_defaults(run=tuning)
    args = parser.parse_args()
    args.run(args)


if __name__ == '__main__':
    main()
