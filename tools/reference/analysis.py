"""Measurements for comparing a machine with the upstream module it follows.

A harness is a host executable that renders one setting to a file of 32-bit
floats (see tools/reference/<module>/reference.cpp and probe.cpp). The two
sides run at different sample rates, so every measurement here is in seconds
and hertz. Noise-excited sounds differ strike by strike: compare them with
`averaged`, over several seeds.
"""
import os
import subprocess
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]


class Harness:
    """One built executable of local/reference/<module>/ and the rate it renders at."""

    def __init__(self, module, name, rate):
        suffix = '.exe' if os.name == 'nt' else ''
        self.path = ROOT / 'local' / 'reference' / module / (name + suffix)
        self.rate = rate
        if not self.path.exists():
            raise SystemExit(f'{self.path} is missing: python tools/reference/build.py {module}')

    def render(self, *arguments, seed=None, extra=()):
        """Runs the harness with its arguments, then the output file, the seed and any extra arguments
        (which need a seed); returns the samples."""
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / 'out.f32'
            tail = [str(out)] + ([str(seed)] if seed is not None else []) + [str(e) for e in extra]
            subprocess.run([str(self.path), *map(str, arguments), *tail], check=True, capture_output=True)
            return np.fromfile(out, np.float32).astype(np.float64)


def window(x, rate, start, duration):
    a = int(start * rate)
    return x[a:a + int(duration * rate)]


def rms(x, rate, start, end):
    segment = window(x, rate, start, end - start)
    return float(np.sqrt(np.mean(segment ** 2))) if len(segment) else 0.0


def envelope(x, rate, step=0.05, until=1.0):
    """RMS in consecutive windows of `step` seconds."""
    return np.array([rms(x, rate, t, t + step) for t in np.arange(0, until - 1e-9, step)])


def levels(x, rate, frequencies, start=0.05, duration=0.2):
    """Magnitudes at the given frequencies: the Hann-windowed spectrum's peak within two bins of each."""
    segment = window(x, rate, start, duration)
    spectrum = np.abs(np.fft.rfft(segment * np.hanning(len(segment)))) / len(segment)
    bins = np.fft.rfftfreq(len(segment), 1 / rate)
    out = []
    for f in frequencies:
        i = int(np.argmin(np.abs(bins - f)))
        out.append(spectrum[max(i - 2, 0):i + 3].max())
    return np.array(out)


def harmonics(x, rate, hz, count=8, start=0.05, duration=0.2):
    """Harmonics 1..count in dB relative to the fundamental."""
    h = levels(x, rate, [k * hz for k in range(1, count + 1)], start, duration)
    return 20 * np.log10(h / h[0] + 1e-12)


def bands(x, rate, edges=tuple(20 * 2 ** k for k in range(11)), seconds=1.0):
    """Energy per band, in dB, over the first `seconds`."""
    segment = x[:int(seconds * rate)]
    power = np.abs(np.fft.rfft(segment)) ** 2 / len(segment)
    f = np.fft.rfftfreq(len(segment), 1 / rate)
    return np.array([10 * np.log10(power[(f >= a) & (f < b)].sum() + 1e-20) for a, b in zip(edges, edges[1:])])


def fundamental_cents(x, rate, hz, start=0.05, duration=0.25, span=100, step=0.25):
    """The strongest partial within `span` cents of `hz`, in cents from it: a Hann-windowed DFT searched
    in `step`-cent steps. Use this for tuning; autocorrelation mixes in the upper partials."""
    segment = window(x, rate, start, duration)
    segment = segment * np.hanning(len(segment))
    t = np.arange(len(segment)) / rate
    cents = np.arange(-span, span + step, step)
    power = [abs(np.sum(segment * np.exp(-2j * np.pi * hz * 2 ** (c / 1200) * t))) for c in cents]
    return float(cents[int(np.argmax(power))])


def brightness(x, rate, start=0.05, end=0.4, reference_rate=48000):
    """The first difference's energy over the signal's, scaled to `reference_rate`: it grows with the
    square of the sample period, so this compares sounds rendered at different rates."""
    segment = window(x, rate, start, end - start)
    d = np.sum(np.diff(segment) ** 2)
    return float(d / (np.sum(segment ** 2) + 1e-30) * (rate / reference_rate) ** 2)


def averaged(measure, seeds):
    """The RMS mean of a measurement (number or array) over renders with these seeds: measure(seed)."""
    values = np.array([measure(seed) for seed in seeds], dtype=np.float64)
    return np.sqrt(np.mean(values ** 2, axis=0))


def seeds(count, first=1000):
    return [first + 7919 * k for k in range(count)]
