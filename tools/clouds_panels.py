# Writes the panel.svg.fragment of the machines after Clouds: the SDK's
# two-page header, then each machine's own nodes, which display.cpp moves and
# reshapes every frame. From the repository root, with the SDK cloned beside it:
#   python tools/clouds_panels.py stratus machines/stratus/panel.svg.fragment [--sdk ../open-xy]
#   python tools/clouds_panels.py cirrus machines/cirrus/panel.svg.fragment [--sdk ../open-xy]
import argparse
from pathlib import Path

# Both machines' shades, darkest to brightest: display.cpp's Shade.
SHADES = ['#262626', '#3A3A3A', '#555555', '#7A7A7A', '#A6A6A6', '#D2D2D2', '#FFFFFF']
PARKED = 'matrix(1 0 0 1 640 120)'           # off screen until a frame places it


def header(sdk, machine, first_node):
    text = (Path(sdk) / 'sdk/templates/shared/page_header.svg.in').read_text(encoding='utf-8')
    for i in range(8):
        text = text.replace('@NODE_%d@' % i, str(first_node + i))
    return text.replace('@ID@', machine).rstrip('\n')


def circles(machine, first, count, radius, colour):
    return [f'  <circle id="{machine}_node_{first + i}" transform="{PARKED}" cx="0" cy="0" r="{radius}" '
            f'fill="{colour}" stroke="{colour}" stroke-width="0.5"/>' for i in range(count)]


def bars(machine, first, count, half_width, half_height, colour):
    return [f'  <path id="{machine}_node_{first + i}" transform="{PARKED}" '
            f'd="M-{half_width} -{half_height} L{half_width} -{half_height} L{half_width} {half_height} L-{half_width} {half_height} Z" '
            f'fill="{colour}" stroke="{colour}" stroke-width="0.5"/>' for i in range(count)]


def palette(machine, colours):
    return [f'  <circle id="{machine}_palette_{i}" transform="{PARKED}" cx="0" cy="0" r="1" '
            f'fill="{colour}" stroke="{colour}" stroke-width="1"/>' for i, colour in enumerate(colours)]


def stratus(sdk):
    # display.cpp's nodes: 0..7 the header, then in drawing order 8 the tape
    # (40..440 by 55..205), 9..104 its bars, 105 the record head, 106 Position's
    # marker, 107..130 the grains' halos, 131..178 two trail dots behind each
    # grain, 179..202 the grains.
    return ['<g id="stratus_panel">', header(sdk, 'stratus', 0),
            '  <!-- Opaque below the header. -->',
            '  <path d="M0,38 L480,38 L480,220 L0,220 Z" fill="#000000"/>',
            '  <!-- A second of the recording as a tape, now at its right end. -->',
            f'  <path id="stratus_node_8" transform="matrix(1 0 0 1 0 0)" d="M40 55 L440 55 L440 205 L40 205 Z" '
            f'fill="none" stroke="{SHADES[2]}" stroke-width="1"/>',
            "  <!-- The tape's bars, standing for the send as it is recorded. -->",
            *bars('stratus', 9, 96, 1.2, 75, SHADES[1]),
            '  <!-- The record head. -->',
            *bars('stratus', 105, 1, 0.75, 80, SHADES[6]),
            '  <!-- Where Position starts the grains: a notch above the tape. -->',
            f'  <path id="stratus_node_106" transform="{PARKED}" d="M-5 -6 L5 -6 L0 0 Z" '
            f'fill="{SHADES[5]}" stroke="{SHADES[5]}" stroke-width="0.5"/>',
            "  <!-- Reverb's halos, the grains' trails and the grains. -->",
            *circles('stratus', 107, 24, 8, SHADES[0]), *circles('stratus', 131, 48, 8, SHADES[2]),
            *circles('stratus', 179, 24, 8, SHADES[6]),
            '  <!-- Palette entries, parked off screen. -->', *palette('stratus', SHADES), '</g>']


def cirrus(sdk):
    # display.cpp's nodes: 0..3 the waveform's four outlines (paths), 4..11 the
    # header, then in drawing order 12..27 the waveform lit under each grain,
    # 28 where the scans start, 29 and 30 the trim's ends, 31..34 the held
    # notes' playheads, 35..66 two trail dots behind each grain, 67..82 the
    # grains, 83..178 a bar for each of the overview's 96 bins, drawn first;
    # text slot 16 asks for a sample.
    lines = ['<g id="cirrus_panel">', header(sdk, 'cirrus', 4),
             "  <!-- Opaque below the header: the stock sampler's page and its waveform stay hidden. -->",
             '  <path d="M0,38 L480,38 L480,220 L0,220 Z" fill="#000000"/>',
             "  <!-- A bar for each of the sample's bins, under its outline. -->",
             *bars('cirrus', 83, 96, 1.2, 60, SHADES[0]),
             '  <!-- The sample, in four outlines of 25 of its 96 bins each (paths 0 to 3), at rest flat,',
             '       each about its own centre, as display.cpp draws them. -->']
    for p in range(4):
        centre = 40 + (24 * p + 12) * 400 / 95
        top = [((24 * p + i) * 400 / 95 + 40 - centre, -1) for i in range(25)]
        bottom = [(x + 0.001, 1) for x, _ in reversed(top)]
        d = 'M' + ' L'.join(f'{x:.4f}'.rstrip('0').rstrip('.') + f' {y}' for x, y in top + bottom) + ' Z'
        lines.append(f'  <path id="cirrus_node_{p}" transform="matrix(1 0 0 1 {centre:.4f} 130)" d="{d}" '
                     f'fill="none" stroke="{SHADES[2]}" stroke-width="1"/>')
    lines += ['  <!-- The waveform lit under each grain: a bar as tall as the sample, scaled down. -->',
              *bars('cirrus', 12, 16, 1, 60, SHADES[3]),
              "  <!-- Where every note's scan starts: the trim's start and Position. -->",
              *bars('cirrus', 28, 1, 0.5, 66, SHADES[1]),
              "  <!-- The trim's start and end, which Shift's first two encoders move. -->",
              *bars('cirrus', 29, 2, 0.75, 64, SHADES[5]),
              '  <!-- A playhead for each held note: where its scan starts the grains. -->',
              *bars('cirrus', 31, 4, 0.5, 66, SHADES[3]),
              '  <!-- Two trail dots behind each grain, along what it has read. -->',
              *circles('cirrus', 35, 32, 8, SHADES[2]),
              '  <!-- The grains, over everything else. -->', *circles('cirrus', 67, 16, 8, SHADES[6]),
              '  <!-- Shown while the track has no sample. -->',
              f'  <text id="cirrus_text_16" transform="matrix(1 0 0 1 200 128)" fill="{SHADES[4]}" '
              'font-family="UniversNextPro-Light" font-size="13px">-</text>',
              '  <!-- Palette entries, parked off screen. -->', *palette('cirrus', SHADES), '</g>']
    return lines


parser = argparse.ArgumentParser()
parser.add_argument('machine', choices=('stratus', 'cirrus'))
parser.add_argument('out')
parser.add_argument('--sdk', default=str(Path(__file__).resolve().parents[2] / 'open-xy'))
args = parser.parse_args()
lines = (stratus if args.machine == 'stratus' else cirrus)(args.sdk)
with open(args.out, 'w', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')
