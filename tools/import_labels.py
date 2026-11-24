#!/usr/bin/env python3
"""Import external block labels into `_authority/labels-<bank>.toml`.

The compendium format (Novalia Spirit's bank write-ups, romhacking.net /
ff6hacking) lists one instruction per line under a bank-qualified address,
with the prose name of a block on the line directly above it:

    ; some context comment
    Draw a menu window
    C3/0375: 4A              LSR A
    C3/0376: 8D 00 21        STA $2100

A label is only a *name*, never a root: an address is imported only when the
project already treats it as a function entry (`recomp/candidate-roots-NN.toml`,
which `tools/gen_symbols.py` derives from byte-verified call targets). That
keeps this file from inventing entry points and from naming a byte in the
middle of an instruction.

Usage:
    python3 tools/import_labels.py --disasm <ff6_bank_c3.txt> --bank C3
    python3 tools/import_labels.py --disasm ... --bank C3 --out /tmp/try.toml

The output is deterministic: same input, same bytes, no timestamps.
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# C3/0375:<tab>4A ...
INSTRUCTION_RE = re.compile(r'^(?P<bank>[0-9A-F]{2})/(?P<addr>[0-9A-F]{4}):\t')

# C identifiers we must not shadow, plus a few framework host symbols.
RESERVED = {
    'auto', 'break', 'case', 'char', 'const', 'continue', 'default', 'do',
    'double', 'else', 'enum', 'extern', 'float', 'for', 'goto', 'if', 'inline',
    'int', 'long', 'register', 'restrict', 'return', 'short', 'signed',
    'sizeof', 'static', 'struct', 'switch', 'typedef', 'union', 'unsigned',
    'void', 'volatile', 'while', 'bool', 'true', 'false', 'main', 'exit',
    'free', 'malloc', 'abs', 'index', 'time', 'printf',
}

MAX_SLUG = 48

# The compendium is a Windows-1252 text file: 0x85 is an ellipsis, 0xF7 a
# division sign, 0xAB/0xBB angle quotes. Fold them so every name and note stays
# ASCII, which keeps the generated C comments encoding-proof.
TRANSLIT = {
    0x00b7: '-', 0x2018: "'", 0x2019: "'", 0x201c: '"', 0x201d: '"',
    0x2026: '...', 0x00f7: '/', 0x00d7: 'x', 0x00ab: '"', 0x00bb: '"',
    0x00b0: 'deg', 0x2013: '-', 0x2014: '-', 0x00e9: 'e', 0x00a0: ' ',
}


def read_blocks(path: pathlib.Path) -> list[tuple[int, str]]:
    """Return (addr, prose) for every labelled block in one bank file."""
    blocks: list[tuple[int, str]] = []
    pending: str | None = None
    text = path.read_bytes().decode('cp1252', errors='replace').translate(TRANSLIT)
    for raw in text.splitlines():
        line = raw.rstrip()
        if not line:
            continue
        m = INSTRUCTION_RE.match(line)
        if m:
            if pending is not None:
                blocks.append((int(m.group('addr'), 16), pending))
                pending = None
            continue
        if re.match(r'^[0-9A-F]{2}/', line):
            continue  # continuation of an instruction's operand/comment
        if raw[:1] in (' ', '\t'):
            continue  # indented hex data dump, not a block name
        text = line.strip()
        if not text or text.startswith(';'):
            continue
        pending = text  # only the last prose line before the address names it
    return blocks


SKIP_PROSE = {'entry points', 'entry point'}


def clean_prose(text: str) -> str:
    text = text.strip()
    if text.startswith('Fork:'):
        text = text[5:].strip()
    while text.endswith('...'):
        text = text[:-3].rstrip()
    return text


def slugify(text: str, addr: int, taken: set[str]) -> str:
    slug = re.sub(r'[^a-z0-9]+', '_', clean_prose(text).lower()).strip('_')
    if not slug:
        slug = f'c3_{addr:04x}'
    if slug[0].isdigit():
        slug = 'c3_' + slug
    if len(slug) > MAX_SLUG:
        slug = slug[:MAX_SLUG]
        if '_' in slug:
            slug = slug.rsplit('_', 1)[0]  # never end on a partial word
    slug = slug.rstrip('_') or f'c3_{addr:04x}'
    if slug in RESERVED:
        slug += '_c3'
    if slug not in taken:
        taken.add(slug)
        return slug
    suffixed = f'{slug}_{addr:04x}'
    n = 2
    while suffixed in taken:
        suffixed = f'{slug}_{addr:04x}_{n}'
        n += 1
    taken.add(suffixed)
    return suffixed


def candidate_roots(bank: str) -> dict[int, dict[str, str]]:
    path = ROOT / f'recomp/candidate-roots-{bank.lower()}.toml'
    if not path.exists():
        raise SystemExit(f'import_labels: {path} missing — run tools/gen_symbols.py first')
    text = path.read_text(encoding='utf-8').replace('\r\n', '\n')
    out: dict[int, dict[str, str]] = {}
    for block in text.split('[[func]]')[1:]:
        fields = dict(re.findall(r'(\w+)\s*=\s*(.+)', block))
        addr = int(fields['addr'].strip().strip('"'), 16)
        out[addr] = {k: v.strip().strip('"') for k, v in fields.items()}
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--disasm', required=True, help='compendium text file for one bank')
    ap.add_argument('--bank', default='C3', help='bank the compendium covers (default C3)')
    ap.add_argument('--out', default=None,
                    help='output TOML (default recomp/labels-<bank>.toml)')
    args = ap.parse_args()

    bank = args.bank.upper()
    src = pathlib.Path(args.disasm).expanduser()
    if not src.exists():
        raise SystemExit(f'import_labels: no such file: {src}')
    raw = src.read_bytes()

    roots = candidate_roots(bank)
    blocks = read_blocks(src)

    taken: set[str] = set()
    rows: list[tuple[int, str, str]] = []
    seen: set[int] = set()
    for addr, prose in blocks:
        if addr not in roots or addr in seen:
            continue
        if clean_prose(prose).strip().lower() in SKIP_PROSE:
            continue  # section header in the compendium, not a block name
        seen.add(addr)
        rows.append((addr, slugify(prose, addr, taken), clean_prose(prose)))
    rows.sort()

    unnamed = len(roots) - len(rows)
    out_path = (pathlib.Path(args.out) if args.out
                else ROOT / f'recomp/labels-{bank.lower()}.toml')

    digest = hashlib.sha256(raw).hexdigest()
    lines = [
        f'# Block names for Final Fantasy VI bank ${bank}.',
        '#',
        '# Imported by tools/import_labels.py from an external commented',
        '# disassembly; regenerate rather than hand-edit the generated block.',
        '#',
        '# Names by Novalia Spirit (Jame Coban), "Final Fantasy VI Bank C3',
        '# Compendium" (2016). Not redistributed with this project - see',
        '# README.md, "Credits".',
        '#',
        f'# source:      {src.name}',
        f'# source_sha:  {digest}',
        f'# source_size: {len(raw)} bytes',
        f'# entries:     {len(rows)} of {len(roots)} known ${bank} function entries',
        f'# unnamed:     {unnamed}',
        '#',
        '# `name` is a C identifier safe to use as a generated function name.',
        '# `note` is the compendium prose, verbatim except for a stripped',
        '# "Fork: " prefix and trailing ellipsis.',
        '#',
        '# Apply with: python3 tools/apply_labels.py   (after tools/gen_roots.py)',
        '',
    ]
    for addr, name, note in rows:
        lines.append('[[label]]')
        lines.append(f'addr = "{addr:04x}"')
        lines.append(f'bank = 0x{bank}')
        lines.append(f'name = "{name}"')
        escaped = note.replace('\\', '\\\\').replace('"', '\\"')
        lines.append(f'note = "{escaped}"')
        lines.append('')

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text('\n'.join(lines).rstrip('\n') + '\n',
                        encoding='utf-8', newline='\n')

    print(f'{len(rows)} labels for {len(roots)} ${bank} entries '
          f'({unnamed} entries still address-named) -> {out_path}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
