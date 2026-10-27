# Counts the Blackfin instructions in a machine's loops, from the pinned
# compiler's output with the SDK's native flags: the per-iteration cost of
# every zero-overhead hardware loop (LSETUP) and of every loop the compiler
# left as a backward branch. Multiply a hot loop's count by its iterations per
# 64-sample block for a per-block figure. Instructions, not cycles, and no
# substitute for a hardware measurement; but unlike sdk.py test's host-timed
# estimate it does not overstate q31::multiply, one instruction on the Blackfin.
# From the repository root:
#   python tools/blackfin_loops.py machines/chimes [--sdk ../open-xy] [--assembly out.s] [--all]
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-mcpu=bf707', '-O2', '-std=gnu++11', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
         '-fno-threadsafe-statics', '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables',
         '-fno-unwind-tables', '-fno-common', '-fno-tree-loop-distribute-patterns',
         '-fomit-frame-pointer', '-nostdinc++']  # tooling/native.py CXX_FLAGS, less -Wall -Werror


def entry(machine):
    """A translation unit that emits the machine's audio callback out of line."""
    record = json.loads((machine / 'machine.json').read_text(encoding='utf-8'))
    definition = json.loads((machine / 'definition.json').read_text(encoding='utf-8'))
    ns = definition['id']
    if record['kind'] == 'synth':
        return (f'#include "engine.cpp"\nvoid entry({ns}::Voice& v, {ns}::Shared& s, opxy::u32 o, opxy::Output out,'
                f' opxy::Parameters p, opxy::Pitch pitch) {{ {ns}::render(v, s, o, out, p, pitch); }}\n')
    if record['kind'] == 'effect':
        resources = definition['resources']
        buffer = 'opxy::Buffer b, ' if resources.get('bufferBytes') else ''
        transport = 'const opxy::Transport& t, ' if resources.get('transport') else ''
        call = ('b, ' if buffer else '') + ('t, ' if transport else '')
        return (f'#include "fx.cpp"\nvoid entry({ns}::State& s, {buffer}{transport}opxy::Channel l, opxy::Channel r,'
                f' opxy::Parameters p) {{ {ns}::process(s, {call}l, r, p); }}\n')
    return (f'#include "sample.cpp"\nvoid entry({ns}::Voice& v, const opxy::SampleBlock& b) {{'
            f' {ns}::render_sample(v, b); }}\n')


def instruction(line):
    s = line.strip()
    return bool(s) and not s.startswith(('//', '.', '#')) and not re.match(r'^[.\w]+:$', s)


def main():
    parser = argparse.ArgumentParser(description='Blackfin instruction counts of a machine\'s loops')
    parser.add_argument('machine')
    parser.add_argument('--sdk', default=str(ROOT.parent / 'open-xy'))
    parser.add_argument('--assembly', help='also keep the assembly here')
    parser.add_argument('--all', action='store_true', help='include loops without multiplies')
    args = parser.parse_args()
    machine, sdk = Path(args.machine).resolve(), Path(args.sdk).resolve()
    cxx = sdk / 'local' / 'toolchain' / 'bin' / ('bfin-elf-g++' + ('.exe' if os.name == 'nt' else ''))
    if not cxx.exists():
        sys.exit(f'{cxx} is missing: python {sdk / "sdk.py"} toolchain fetch')
    with tempfile.TemporaryDirectory() as folder:
        source, assembly = Path(folder) / 'entry.cpp', Path(args.assembly or Path(folder) / 'entry.s')
        source.write_text(entry(machine), encoding='utf-8')
        subprocess.run([str(cxx), *FLAGS, '-I' + str(sdk / 'sdk' / 'include'),
                        '-I' + str(sdk / 'sdk' / 'platform' / 'blackfin' / 'freestanding'), '-I' + str(machine),
                        '-S', str(source), '-o', str(assembly)], check=True)
        lines = assembly.read_text().splitlines()
    labels = {m.group(1): i for i, line in enumerate(lines) if (m := re.match(r'^(\.L\d+):', line.strip()))}
    found = []
    for i, line in enumerate(lines):
        hardware = re.search(r'LSETUP \((\.L\d+), (\.L\d+)\)', line)
        branch = re.search(r'if !?cc jump (\.L\d+)', line)
        if hardware:
            body = lines[labels[hardware.group(1)]:labels[hardware.group(2)] + 2]
            kind = 'hardware loop'
        elif branch and branch.group(1) in labels and 0 < i - labels[branch.group(1)] < 400:
            body = lines[labels[branch.group(1)]:i + 1]
            kind = 'branch loop  '
        else:
            continue
        body = [l.strip() for l in body if instruction(l)]
        multiplies = sum('(T)' in l or '*' in l for l in body)
        if multiplies or args.all:
            found.append((i + 1, kind, len(body), multiplies))
    print('Branch loops count both arms of any condition inside them; read the assembly for the hot path.')
    for line, kind, count, multiplies in found:
        print(f'assembly line {line:5d}: {kind} of {count:3d} instructions, {multiplies:2d} multiplies')


if __name__ == '__main__':
    main()
