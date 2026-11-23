#!/usr/bin/env python3
"""Compose recomp/symbols.toml from the seed vectors plus candidate root lists.

    python3 tools/gen_roots.py --bank C2 --lo 0 --hi 425          # all banks done + C2
    python3 tools/gen_roots.py --bank none                        # seed only

`--done` lists the banks promoted in full (they passed the frame oracle on the
route that exercises them). The bank under test contributes roots [--lo,--hi) so a
failing promotion can be bisected by index. The file is rebuilt from HEAD's seed
symbols.toml every run, so nothing depends on a previously edited file, and
`git checkout -- recomp/symbols.toml` restores the committed state.

Candidate lists come from `tools/gen_symbols.py --bank NN --emit`; for FF6 the
ld65 .dbg is not needed (banks come from the call target's own address, names are
address-derived) — see STATUS.md Step 13.
"""
import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def blocks(path):
    text = path.read_text(encoding='utf-8').replace('\r\n', '\n')
    return [b for b in text.split('[[func]]')[1:] if b.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bank', default='none', help='bank under test, or "none"')
    ap.add_argument('--lo', type=int, default=0)
    ap.add_argument('--hi', type=int, default=0, help='0 = all roots of --bank')
    ap.add_argument('--done', default='C0,C1',
                    help='comma-separated banks promoted in full')
    args = ap.parse_args()

    seed = subprocess.run(['git', '-C', str(ROOT), 'show', 'HEAD:recomp/symbols.toml'],
                          capture_output=True, text=True, check=True).stdout
    n_seed = seed.count('[[func]]')
    assert n_seed == 3, f'seed symbols.toml expected 3 vectors, got {n_seed}'

    out = seed.rstrip('\n') + '\n'
    total = n_seed
    for bank in [b.strip().upper() for b in args.done.split(',') if b.strip()]:
        path = ROOT / f'recomp/candidate-roots-{bank.lower()}.toml'
        if not path.exists():
            print(f'  {bank}: no candidate list ({path.name}) — skipped', file=sys.stderr)
            continue
        roots = blocks(path)
        out += f'\n# --- bank {bank} roots (promoted, oracle-clean) ---\n'
        for r in roots:
            out += '[[func]]' + r.rstrip() + '\n\n'
        total += len(roots)
        print(f'  {bank}: {len(roots)} roots')

    bank = args.bank.upper()
    if bank not in ('NONE', ''):
        cand = blocks(ROOT / f'recomp/candidate-roots-{bank.lower()}.toml')
        hi = args.hi or len(cand)
        sel = cand[args.lo:hi]
        out += f'# --- bank {bank} trial roots [{args.lo}:{hi}) ---\n'
        for r in sel:
            out += '[[func]]' + r.rstrip() + '\n\n'
        total += len(sel)
        print(f'  {bank}: {len(sel)} of {len(cand)} roots [{args.lo}:{hi})')

    (ROOT / 'recomp/symbols.toml').write_text(out, encoding='utf-8', newline='\n')
    print(f'wrote {total} funcs to recomp/symbols.toml')
    return 0


if __name__ == '__main__':
    sys.exit(main())
