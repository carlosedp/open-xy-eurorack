# Stratus against upstream Clouds. From the repository root, after
# tools/reference/upstream.py and tools/reference/build.py clouds:
#   python tools/reference/clouds/compare.py level [--seeds 4] [--quick]
#   python tools/reference/clouds/compare.py rate|pitch|delay|freeze|feedback|reverb [--seeds 4]
#   python tools/reference/clouds/compare.py all
# Both sides take the same input signal on both channels, rendered at each
# side's rate, and write Clouds' stereo output, all wet, in its units (1.0 is
# full scale); the checks read its left channel. Measurements start once the
# recording has filled, 1.2 s in. Grains seeded at random differ from run to
# run: those settings are averaged over --seeds runs.
import argparse
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import analysis  # noqa: E402

WARM = 1.2  # seconds before measuring


def sides():
    return analysis.Harness('clouds', 'reference', 32000), analysis.Harness('clouds', 'probe', 44100)


def tone(hz, amplitude=0.5):
    return lambda t: amplitude * np.sin(2 * np.pi * hz * t)


def render(harness, signal, seconds, position, size, density, texture, pitch=0.505, feedback=0.0, reverb=0.0,
           freeze_at=-1, seed=None, channel=0):
    """One channel of a side's output: 0 left, 1 right."""
    t = np.arange(int(seconds * harness.rate) + 64) / harness.rate
    with tempfile.TemporaryDirectory() as folder:
        source = Path(folder) / 'in.f32'
        signal(t).astype(np.float32).tofile(source)
        x = harness.render(position, size, density, texture, pitch, feedback, reverb, freeze_at, seconds, source,
                           seed=seed)
        return x[channel::2]


def both(signal, seconds, settings, seeds, measure, **options):
    """measure(x, rate) on each side, averaged over seeds; returns (reference, probe)."""
    return tuple(analysis.averaged(lambda seed: measure(render(h, signal, seconds, *settings, seed=seed, **options),
                                                         h.rate), seeds) for h in sides())


def level(args):
    """RMS of a 220 Hz tone through the grains, all wet, over a grid of Density, Size and Texture."""
    densities = (0.3, 0.4, 0.65, 0.8, 0.9) if not args.quick else (0.3, 0.8)
    sizes = (0.2, 0.5, 0.8) if not args.quick else (0.5,)
    textures = (0.2, 0.6, 0.9) if not args.quick else (0.6,)
    ratios = []
    for density in densities:
        for size in sizes:
            for texture in textures:
                seeds = analysis.seeds(1 if density < 0.5 else args.seeds)
                r, c = both(tone(220), WARM + 1.5, (0.3, size, density, texture), seeds,
                            lambda x, rate: analysis.rms(x, rate, WARM, WARM + 1.5))
                ratios.append(c / max(r, 1e-9))
                print(f'density {density:<4} size {size:<4} texture {texture:<4} | RMS {r:.4f} {c:.4f} '
                      f'({c / max(r, 1e-9):5.2f}x)')
    ratios.sort()
    print(f'Stratus / Clouds RMS median {ratios[len(ratios) // 2]:.2f}, range {ratios[0]:.2f}..{ratios[-1]:.2f}')


def onset_interval(x, rate):
    """The median time between grain onsets, in seconds: rising crossings of a fifth of the peak by the
    output's RMS in 1 ms windows, which a 3 kHz input does not ripple. For grains that do not overlap."""
    segment = analysis.window(x, rate, WARM, 3.0) ** 2
    step = int(rate / 1000)
    e = np.sqrt(segment[:len(segment) // step * step].reshape(-1, step).mean(axis=1))
    above = e > 0.2 * e.max()
    onsets = np.flatnonzero(above[1:] & ~above[:-1])
    return float(np.median(np.diff(onsets)) * step / rate) if len(onsets) > 2 else float('nan')


def rate(args):
    """Below Density's middle grains start at regular intervals: the interval, in seconds, where they are
    sparse enough not to overlap."""
    for density in (0.33, 0.37, 0.4):
        for size in (0.1, 0.25):
            r, c = (onset_interval(render(h, tone(3000), WARM + 3.0, 0.2, size, density, 0.5), h.rate)
                    for h in sides())
            print(f'density {density:<4} size {size:<4} | interval {r * 1000:7.1f} ms {c * 1000:7.1f} ms '
                  f'({c / r:5.2f}x)')


def grain_cents(x, rate, hz):
    """The pitch inside the grains, in cents from `hz`: the median period between upward zero crossings,
    interpolated, where the output is loud. A spectrum would not do: each grain restarts the tone at a
    new phase, which splits it into lines spaced by the grain rate."""
    segment = analysis.window(x, rate, WARM, 1.5)
    expected = rate / hz
    width = int(2 * expected)
    envelope = np.sqrt(np.convolve(segment ** 2, np.ones(width) / width, 'same'))
    loud = envelope > 0.3 * envelope.max()
    i = np.flatnonzero((segment[:-1] < 0) & (segment[1:] >= 0))
    crossings = i + segment[i] / (segment[i] - segment[i + 1])
    periods = np.diff(crossings)[loud[i[1:]] & loud[i[:-1]]]
    periods = periods[(periods > 0.8 * expected) & (periods < 1.25 * expected)]
    return float(1200 * np.log2(expected / np.median(periods)))


def pitch(args):
    """A 440 Hz tone transposed by Pitch's notches: the pitch inside the grains."""
    # Clouds' knob at the start of its notches (lut_quantized_pitch's segments
    # 1, 2, 7, 13 and 14), where its table holds the notch; Cirrus' on the
    # whole values it moves them to.
    for clouds, cirrus, semitones in ((68.5 / 1024, 7 / 99, -12), (136.5 / 1024, 13 / 99, -7), (0.505, 50 / 99, 0),
                                      (888.5 / 1024, 86 / 99, 7), (956.5 / 1024, 92 / 99, 12)):
        expected = 440 * 2 ** (semitones / 12)
        # Long grains at regular intervals, barely overlapping: each a clean tone burst.
        r, c = (grain_cents(render(h, tone(440), WARM + 1.6, 0.2, 0.85, 0.3, 0.6, pitch=knob), h.rate, expected)
                for h, knob in zip(sides(), (clouds, cirrus)))
        print(f'pitch {semitones:+3d} st | {expected:7.1f} Hz: Clouds {r:+5.1f} cents, Stratus {c:+5.1f} cents')


def delay(args):
    """A 20 ms burst after silence: how long after it the grains play it back, by Position."""
    burst_at = 1.5

    def burst(t):
        return np.where((t >= burst_at) & (t < burst_at + 0.02), 0.5 * np.sin(2 * np.pi * 1000 * t), 0.0)

    def centre(x, rate):
        segment = analysis.window(x, rate, burst_at, 1.2) ** 2
        return float(np.sum(segment * np.arange(len(segment)) / rate) / (np.sum(segment) + 1e-20))

    for position in (0.0, 0.25, 0.5, 0.75):
        r, c = both(burst, burst_at + 1.2, (position, 0.15, 0.95, 0.5), analysis.seeds(args.seeds), centre)
        print(f'position {position:<4} | heard {r * 1000:6.1f} ms after the burst, Stratus {c * 1000:6.1f} ms')


def freeze(args):
    """330 Hz, frozen at 1.4 s, then 550 Hz: the frozen cloud keeps 330 Hz."""
    def switch(t):
        return np.where(t < 1.5, 0.5 * np.sin(2 * np.pi * 330 * t), 0.5 * np.sin(2 * np.pi * 550 * t))

    def held(x, rate):
        a, b = analysis.levels(x, rate, (330, 550), start=2.0, duration=0.5)
        return np.array([a, b])

    r, c = both(switch, 2.6, (0.3, 0.4, 0.8, 0.6), analysis.seeds(args.seeds), held, freeze_at=1.4)
    print(f'frozen at 330 Hz, then 550 Hz in | 330 Hz {r[0]:.4f} {c[0]:.4f}, 550 Hz {r[1]:.4f} {c[1]:.4f}')


BURST_AT = 1.5


def burst(t):
    """A 20 ms, 1 kHz burst after silence."""
    return np.where((t >= BURST_AT) & (t < BURST_AT + 0.02), 0.5 * np.sin(2 * np.pi * 1000 * t), 0.0)


def tail(x, rate):
    """RMS after the burst: 0.1 to 0.3 s, 0.6 to 0.8 s and 1.2 to 1.4 s."""
    return np.array([analysis.rms(x, rate, BURST_AT + a, BURST_AT + a + 0.2) for a in (0.1, 0.6, 1.2)])


def feedback(args):
    """Feedback repeats a burst through the grains: its tail, by Feedback."""
    for amount in (0.3, 0.6, 0.9):
        r, c = both(burst, BURST_AT + 1.5, (0.1, 0.2, 0.8, 0.5), analysis.seeds(args.seeds), tail, feedback=amount)
        print(f'feedback {amount:<4} | tail RMS ' + ', '.join(f'{a:.4f} {b:.4f}' for a, b in zip(r, c)))


def reverb(args):
    """Clouds' reverb after a burst, on both channels: its tail, by Reverb."""
    for amount in (0.3, 0.6, 0.9):
        for channel in (0, 1):
            r, c = (analysis.averaged(lambda seed: tail(render(h, burst, BURST_AT + 1.5, 0.1, 0.2, 0.8, 0.5,
                                                               reverb=amount, seed=seed, channel=channel), h.rate),
                                      analysis.seeds(args.seeds)) for h in sides())
            print(f'reverb {amount:<4} {"LR"[channel]} | tail RMS ' + ', '.join(f'{a:.4f} {b:.4f}' for a, b in zip(r, c)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('check', choices=['level', 'rate', 'pitch', 'delay', 'freeze', 'feedback', 'reverb', 'all'])
    parser.add_argument('--seeds', type=int, default=4)
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args()
    checks = [level, rate, pitch, delay, freeze, feedback, reverb] if args.check == 'all' else [globals()[args.check]]
    for check in checks:
        print(f'== {check.__name__}: {check.__doc__}')
        check(args)


if __name__ == '__main__':
    main()
