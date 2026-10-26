# Runs `sdk.py test` with its host compiles as C++14 instead of C++11. Only
# for hosts whose C++ library needs C++14: clang++ on Windows uses Microsoft's,
# whose <vector> and <chrono> do not compile as C++11, so sdk.py test's host
# tests and CPU estimate fail there. Everything else, including -Wall -Werror,
# is unchanged. CI (Linux, g++) needs none of this. Run from the SDK checkout,
# with sdk.py test's arguments:
#   python ../open-xy-eurorack/tools/host/sdk_cxx14.py --machine ../open-xy-eurorack/machines/chimes --cxx clang++ --host-cxx clang++
import runpy
import subprocess
import sys

sys.path.insert(0, '.')
original = subprocess.run


def run(command, **options):
    if isinstance(command, list):
        command = ['-std=c++14' if part == '-std=c++11' else part for part in command]
    return original(command, **options)


subprocess.run = run
sys.argv = ['sdk.py', 'test', *sys.argv[1:]]
runpy.run_path('sdk.py', run_name='__main__')
