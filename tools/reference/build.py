# Builds a module's two host harnesses into local/reference/<module>/:
#   reference  the upstream float code, at its own sample rate
#   probe      this repository's machine, through its engine.cpp or fx.cpp
# Each tools/reference/<module>/build.json names the sources. From the
# repository root, after tools/reference/upstream.py:
#   python tools/reference/build.py rings [--cxx clang++] [--sdk ../open-xy]
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / 'local' / 'upstream'


def compile_(cxx, sources, includes, defines, output, warnings):
    command = [cxx, '-std=c++14', '-O2', *('-D' + d for d in defines), *('-I' + str(i) for i in includes),
               *map(str, sources), '-o', str(output)]
    if not warnings:
        command.insert(3, '-w')
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        sys.exit(f'{output.name} does not compile:\n{result.stdout}{result.stderr}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('module', help='a folder of tools/reference, such as rings')
    parser.add_argument('--cxx', default=shutil.which('clang++') or 'c++', help='host C++14 compiler')
    parser.add_argument('--sdk', default=str(ROOT.parent / 'open-xy'), help='the Open-XY SDK checkout')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent / args.module
    spec = json.loads((here / 'build.json').read_text(encoding='utf-8'))
    out = ROOT / 'local' / 'reference' / args.module
    out.mkdir(parents=True, exist_ok=True)
    suffix = '.exe' if os.name == 'nt' else ''
    # Windows' C runtime warns on fopen and lacks M_PI by default.
    host = ['_CRT_SECURE_NO_WARNINGS', '_USE_MATH_DEFINES']

    reference = spec['reference']
    eurorack, stmlib = UPSTREAM / 'eurorack', UPSTREAM / 'stmlib'
    if not (eurorack / '.git').exists() or not (stmlib / '.git').exists():
        sys.exit('Fetch the upstream code first: python tools/reference/upstream.py')
    compile_(args.cxx, [here / reference['source'], *(eurorack / s for s in reference['eurorack']),
                        *(stmlib / s for s in reference['stmlib'])],
             [UPSTREAM, eurorack], [*host, *reference.get('defines', [])], out / ('reference' + suffix), False)

    probe = spec['probe']
    machine = ROOT / probe['machine']
    compile_(args.cxx, [here / probe['source']], [Path(args.sdk) / 'sdk' / 'include', machine], host,
             out / ('probe' + suffix), True)
    print(f'Built {out / ("reference" + suffix)} and {out / ("probe" + suffix)}')


if __name__ == '__main__':
    main()
