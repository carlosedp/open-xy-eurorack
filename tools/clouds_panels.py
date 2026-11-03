# Writes the panel.svg.fragment of the machines after Clouds: the SDK's
# two-page header, then each machine's own nodes, which display.cpp moves and
# reshapes every frame. From the repository root, with the SDK cloned beside it:
#   python tools/clouds_panels.py stratus machines/stratus/panel.svg.fragment [--sdk ../open-xy]
#   python tools/clouds_panels.py cirrus machines/cirrus/panel.svg.fragment [--sdk ../open-xy]
import argparse
from pathlib import Path

PALETTE = ['#4A4A4A', '#9A9A9A', '#F0F0F0']  # dim, mid and bright
PARKED = 'matrix(1 0 0 1 640 120)'           # off screen until a frame places it


def header(sdk, machine, first_node):
    text = (Path(sdk) / 'sdk/templates/shared/page_header.svg.in').read_text(encoding='utf-8')
    for i in range(8):
        text = text.replace('@NODE_%d@' % i, str(first_node + i))
    return text.replace('@ID@', machine).rstrip('\n')


def circles(machine, first, count, radius):
    return [f'  <circle id="{machine}_node_{first + i}" transform="{PARKED}" cx="0" cy="0" r="{radius}" '
            f'fill="{PALETTE[2]}" stroke="{PALETTE[2]}" stroke-width="0.5"/>' for i in range(count)]


def palette(machine):
    return [f'  <circle id="{machine}_palette_{i}" transform="{PARKED}" cx="0" cy="0" r="1" '
            f'fill="{colour}" stroke="{colour}" stroke-width="1"/>' for i, colour in enumerate(PALETTE)]


def stratus(sdk):
    # display.cpp: nodes 0..7 the header, 8 the recording (40..440 by 100..150),
    # 9 Position's marker, 10..33 the grains.
    return ['<g id="stratus_panel">', header(sdk, 'stratus', 0),
            '  <!-- A second of the recording, now at its right end. -->',
            f'  <path id="stratus_node_8" transform="matrix(1 0 0 1 0 0)" d="M40 100 L440 100 L440 150 L40 150 Z" '
            f'fill="none" stroke="{PALETTE[0]}" stroke-width="1"/>',
            '  <!-- Where Position starts the grains: a notch above the recording. -->',
            f'  <path id="stratus_node_9" transform="{PARKED}" d="M-5 -6 L5 -6 L0 0 Z" '
            f'fill="{PALETTE[1]}" stroke="{PALETTE[1]}" stroke-width="0.5"/>',
            '  <!-- The grains. -->', *circles('stratus', 10, 24, 4),
            '  <!-- Palette entries, parked off screen. -->', *palette('stratus'), '</g>']


def cirrus(sdk):
    # display.cpp: nodes 0..3 the waveform's four outlines (paths), 4..11 the
    # header, 12 and 31..33 the held notes' playheads, 13..28 the grains, 29 and
    # 30 the trim's ends, 34 where the scans start; text slot 16 asks for a sample.
    lines = ['<g id="cirrus_panel">', header(sdk, 'cirrus', 4),
             '  <!-- The sample, in four outlines of 25 of its 96 bins each (paths 0 to 3), at rest flat,',
             '       each about its own centre, as display.cpp draws them. -->']
    for p in range(4):
        centre = 40 + (24 * p + 12) * 400 / 95
        top = [((24 * p + i) * 400 / 95 + 40 - centre, -1) for i in range(25)]
        bottom = [(x + 0.001, 1) for x, _ in reversed(top)]
        d = 'M' + ' L'.join(f'{x:.4f}'.rstrip('0').rstrip('.') + f' {y}' for x, y in top + bottom) + ' Z'
        lines.append(f'  <path id="cirrus_node_{p}" transform="matrix(1 0 0 1 {centre:.4f} 130)" d="{d}" '
                     f'fill="none" stroke="{PALETTE[0]}" stroke-width="1"/>')
    lines += ['  <!-- A playhead for each held note: where its scan starts the grains. -->',
              *[f'  <path id="cirrus_node_{n}" transform="{PARKED}" d="M-0.5 -46 L0.5 -46 L0.5 46 L-0.5 46 Z" '
                f'fill="{PALETTE[1]}" stroke="{PALETTE[1]}" stroke-width="0.5"/>' for n in (12, 31, 32, 33)],
              '  <!-- Where every note\'s scan starts: the trim\'s start and Position. -->',
              f'  <path id="cirrus_node_34" transform="{PARKED}" d="M-0.5 -46 L0.5 -46 L0.5 46 L-0.5 46 Z" '
              f'fill="{PALETTE[0]}" stroke="{PALETTE[0]}" stroke-width="0.5"/>',
              '  <!-- The grains. -->', *circles('cirrus', 13, 16, 8),
              "  <!-- The trim's start and end, which Shift's first two encoders move. -->",
              *[f'  <path id="cirrus_node_{n}" transform="{PARKED}" d="M-0.75 -44 L0.75 -44 L0.75 44 L-0.75 44 Z" '
                f'fill="{PALETTE[2]}" stroke="{PALETTE[2]}" stroke-width="0.5"/>' for n in (29, 30)],
              '  <!-- Shown while the track has no sample. -->',
              f'  <text id="cirrus_text_16" transform="matrix(1 0 0 1 200 128)" fill="{PALETTE[1]}" '
              'font-family="UniversNextPro-Light" font-size="13px">-</text>',
              '  <!-- Palette entries, parked off screen. -->', *palette('cirrus'), '</g>']
    return lines


parser = argparse.ArgumentParser()
parser.add_argument('machine', choices=('stratus', 'cirrus'))
parser.add_argument('out')
parser.add_argument('--sdk', default=str(Path(__file__).resolve().parents[2] / 'open-xy'))
args = parser.parse_args()
lines = (stratus if args.machine == 'stratus' else cirrus)(args.sdk)
with open(args.out, 'w', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')
