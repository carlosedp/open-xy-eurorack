# Writes machines/chimes/panel.svg.fragment: four outlines of a rail with three
# chimes each, at rest at harmonic Structure, and the palette. display.cpp reshapes and moves them every
# frame, from the same geometry. From the repository root:
#   python tools/chimes_panel.py machines/chimes/panel.svg.fragment
import math
import sys

LONGEST, SHORTEST, CORD, RAIL = 114, 12, 6, 40
# display.cpp's kChime: (x, y, foot); a foot's y is below the chime's length.
CHIME = [(-0.5, 0, False), (-0.5, 6, False), (-2.5, 6, False), (-2.5, 0, True), (-1.8, 1.8, True), (0, 2.5, True),
         (1.8, 1.8, True), (2.5, 0, True), (2.5, 6, False), (0.5, 6, False), (0.5, 0, False)]
PALETTE = ['#F0F0F0', '#9A9A9A', '#4A4A4A']  # lit, half-lit and unlit chimes


def number(v):
    s = f'{v:.4f}'.rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def length(i):
    return min(LONGEST, max(SHORTEST, LONGEST / math.sqrt(i + 1)))


lines = ['<g id="chimes_panel">',
         '  <!-- The rail and twelve chimes, three to each outline (nodes 0 to 3): each outline is its',
         '       piece of the rail, a 3-pixel bar, with its chimes hanging on cords, each down one side',
         '       of its cord and tube, around its rounded foot and back up. -->']
for p in range(4):
    points = [(-63, -60)]
    for k in range(3):
        i, hang = 3 * p + k, (k - 1) * 40
        points += [(hang + x, -60 + y + (CORD + length(i) if foot else 0)) for x, y, foot in CHIME]
    points += [(63, -60), (63, -63), (21, -63), (-21, -63), (-63, -63)]
    assert len(points) == 39 and len(set(points)) == 39
    d = 'M' + ' L'.join(f'{number(x)} {number(y)}' for x, y in points) + ' Z'
    lines.append(f'  <path id="chimes_node_{p}" transform="matrix(1 0 0 1 {60 + 120 * p} {RAIL + 60})" d="{d}" '
                 f'fill="none" stroke="{PALETTE[0]}" stroke-width="1"/>')
lines.append('  <!-- Palette entries, parked off screen. -->')
for i, colour in enumerate(PALETTE):
    lines.append(f'  <circle id="chimes_palette_{i}" transform="matrix(1 0 0 1 640 120)" cx="0" cy="0" r="1" '
                 f'fill="{colour}" stroke="{colour}" stroke-width="1"/>')
lines.append('</g>')
with open(sys.argv[1], 'w', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')
