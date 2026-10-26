# Fetches the upstream Mutable Instruments code the machines here follow, at
# the pinned commits, into local/upstream/ (ignored by git):
#   local/upstream/eurorack   pichenettes/eurorack (rings, clouds, beads only)
#   local/upstream/stmlib     pichenettes/stmlib (a submodule upstream)
# From the repository root:  python tools/reference/upstream.py
# Already-fetched checkouts are verified, not fetched again.
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / 'local' / 'upstream'
SOURCES = {
    'eurorack': ('https://github.com/pichenettes/eurorack', 'f4cfa7697095a28d292bff758beb51deb49b5a86',
                 ['rings', 'clouds', 'beads']),
    'stmlib': ('https://github.com/pichenettes/stmlib', 'd18def816c51d1da0c108236928b2bbd25c17481', None),
}


def git(*arguments, cwd=None):
    return subprocess.run(['git', *arguments], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def fetch(name, url, commit, folders):
    target = UPSTREAM / name
    if (target / '.git').exists():
        head = git('rev-parse', 'HEAD', cwd=target)
        if head != commit:
            sys.exit(f'{target} is at {head}, not the pinned {commit}: delete it to fetch again')
        return target
    target.mkdir(parents=True)
    git('init', '--quiet', cwd=target)
    git('remote', 'add', 'origin', url, cwd=target)
    if folders:
        git('sparse-checkout', 'set', *folders, cwd=target)
    git('fetch', '--quiet', '--depth', '1', '--filter=blob:none', 'origin', commit, cwd=target)
    git('checkout', '--quiet', 'FETCH_HEAD', cwd=target)
    return target


if __name__ == '__main__':
    for name, (url, commit, folders) in SOURCES.items():
        print(f'{name}: {fetch(name, url, commit, folders)} at {commit[:12]}')
