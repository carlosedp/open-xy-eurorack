# Cirrus against upstream Clouds. From the repository root, after
# tools/reference/upstream.py and tools/reference/build.py clouds:
#   python tools/reference/clouds/cirrus.py level|spread|pitch|all [--seeds 4] [--quick]
# Clouds records a steady tone as it comes in; Cirrus plays a sample of the
# same tone at middle C, scanning it at its own speed (Speed 99), so that both
# read grains of the tone as it flows. A stereo sample is set against Clouds'
# 16-bit stereo quality, a mono one against its 16-bit mono quality. Both
# write Clouds' stereo output, all wet, in its units (1.0 is full scale).
# Measurements start once Clouds' recording has filled, 1.2 s in. Grains
# seeded at random differ from run to run: those settings are averaged over
# --seeds runs.
import argparse
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import analysis  # noqa: E402
from compare import grain_cents, tone  # noqa: E402

WARM, SPAN = 1.2, 1.5  # seconds before measuring, and how long
MODES = {2: 'stereo', 1: 'mono'}


def clouds(signal, position, size, density, texture, pitch=0.505, spread=0.0, channels=2, seed=1):
    """Clouds' output, interleaved, at 32 kHz."""
    h = analysis.Harness('clouds', 'reference', 32000)
    t = np.arange(int((WARM + SPAN) * h.rate) + 64) / h.rate
    with tempfile.TemporaryDirectory() as folder:
        source = Path(folder) / 'in.f32'
        signal(t).astype(np.float32).tofile(source)
        return h.render(position, size, density, texture, pitch, 0, 0, -1, WARM + SPAN, source, seed=seed,
                        extra=(spread, 0 if channels == 2 else 1)), h.rate


def cirrus(signal, position, size, density, texture, pitch=50 / 99, spread=0.0, channels=2, seed=1):
    """Cirrus' output, interleaved, at 44.1 kHz."""
    h = analysis.Harness('clouds', 'cirrus', 44100)
    t = np.arange(int((WARM + SPAN + 1) * h.rate)) / h.rate
    with tempfile.TemporaryDirectory() as folder:
        source = Path(folder) / 'in.f32'
        signal(t).astype(np.float32).tofile(source)
        return h.render(position, size, density, texture, pitch, 1.0, spread, channels, WARM + SPAN, source,
                        seed=seed), h.rate


def averaged(render, measure, seeds, *settings, **options):
    return analysis.averaged(lambda seed: measure(*render(*settings, seed=seed, **options)), seeds)


def left_rms(x, rate):
    return analysis.rms(x[0::2], rate, WARM, WARM + SPAN)


def level(args):
    """RMS of a 220 Hz tone through the grains, without spread, over Density, Size and Texture."""
    densities = (0.3, 0.4, 0.65, 0.8, 0.9) if not args.quick else (0.3, 0.8)
    sizes = (0.2, 0.5, 0.8) if not args.quick else (0.5,)
    textures = (0.2, 0.6) if not args.quick else (0.6,)
    for channels in (2, 1):
        ratios = []
        for density in densities:
            for size in sizes:
                for texture in textures:
                    seeds = analysis.seeds(1 if density < 0.5 else args.seeds)
                    settings = (tone(220), 0.3, size, density, texture)
                    r = averaged(clouds, left_rms, seeds, *settings, channels=channels)
                    c = averaged(cirrus, left_rms, seeds, *settings, channels=channels)
                    ratios.append(c / max(r, 1e-9))
                    print(f'{MODES[channels]:6} density {density:<4} size {size:<4} texture {texture:<4} | '
                          f'RMS {r:.4f} {c:.4f} ({c / max(r, 1e-9):5.2f}x)')
        ratios.sort()
        print(f'{MODES[channels]}: Cirrus / Clouds RMS median {ratios[len(ratios) // 2]:.2f}, '
              f'range {ratios[0]:.2f}..{ratios[-1]:.2f}')


def width(x, rate):
    """The side against the middle: RMS of L - R over RMS of L + R."""
    l, r = (analysis.window(x[c::2], rate, WARM, SPAN) for c in (0, 1))
    return float(np.sqrt(np.mean((l - r) ** 2)) / max(np.sqrt(np.mean((l + r) ** 2)), 1e-12))


def spread(args):
    """Spread pans each grain at random: how wide a 220 Hz tone comes out, and how loud on the left."""
    seeds = analysis.seeds(args.seeds)
    for channels in (2, 1):
        for amount in (0.0, 0.5, 1.0):
            settings = (tone(220), 0.3, 0.5, 0.8, 0.6)
            r = [averaged(clouds, m, seeds, *settings, spread=amount, channels=channels) for m in (width, left_rms)]
            c = [averaged(cirrus, m, seeds, *settings, spread=amount, channels=channels) for m in (width, left_rms)]
            print(f'{MODES[channels]:6} spread {amount:<4} | side/middle {r[0]:.3f} {c[0]:.3f}, '
                  f'left RMS {r[1]:.4f} {c[1]:.4f} ({c[1] / max(r[1], 1e-9):5.2f}x)')


def pitch(args):
    """A 440 Hz tone transposed by Pitch's notches: the pitch inside the grains."""
    for knob_clouds, knob_cirrus, semitones in ((68.5 / 1024, 7 / 99, -12), (136.5 / 1024, 13 / 99, -7),
                                                (0.505, 50 / 99, 0), (888.5 / 1024, 86 / 99, 7),
                                                (956.5 / 1024, 92 / 99, 12)):
        expected = 440 * 2 ** (semitones / 12)
        # Long grains at regular intervals, barely overlapping: each a clean tone burst.
        settings = (tone(440), 0.2, 0.85, 0.3, 0.6)
        x, rate = clouds(*settings, pitch=knob_clouds)
        y, rate_y = cirrus(*settings, pitch=knob_cirrus)
        r, c = grain_cents(x[0::2], rate, expected), grain_cents(y[0::2], rate_y, expected)
        print(f'pitch {semitones:+3d} st | {expected:7.1f} Hz: Clouds {r:+5.1f} cents, Cirrus {c:+5.1f} cents')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('check', choices=['level', 'spread', 'pitch', 'all'])
    parser.add_argument('--seeds', type=int, default=4)
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args()
    checks = [level, spread, pitch] if args.check == 'all' else [globals()[args.check]]
    for check in checks:
        print(f'== {check.__name__}: {check.__doc__}')
        check(args)


if __name__ == '__main__':
    main()
