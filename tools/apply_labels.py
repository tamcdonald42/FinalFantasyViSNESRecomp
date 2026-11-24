#!/usr/bin/env python3
"""Apply `recomp/labels-*.toml` names to `recomp/symbols.toml`.

`tools/gen_roots.py` composes symbols.toml from the seed vectors plus the
candidate root lists, and every root it writes is named after its address
(`sub_C30375`). This pass renames those roots in place, using the block names
imported by `tools/import_labels.py`, and changes nothing else: `emit`,
`entry_mx` and the call-site note are preserved verbatim (the compendium prose
is appended to the note), so the AOT set and the generated code are unchanged.

Order matters:

    python3 tools/gen_roots.py --done C0,C1,C2,C3,EE --bank none
    python3 tools/apply_labels.py
    tools/regen.sh

Usage:
    python3 tools/apply_labels.py            # rename, report, write
    python3 tools/apply_labels.py --check    # report only, do not write

Idempotent: a block whose name is already specific is never renamed again, so
running this twice after one gen_roots is a no-op.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ADDRESS_NAME = re.compile(r'^(?:sub_)?[0-9A-F]{2}[0-9A-F]{4}$|^_[0-9A-F]{4}$')
FIELDS = ('name', 'addr', 'bank', 'emit', 'note', 'entry_mx_at')


def read_labels() -> dict[tuple[int, int], tuple[str, str]]:
    """(bank, addr) -> (c identifier, compendium prose)."""
    labels: dict[tuple[int, int], tuple[str, str]] = {}
    for path in sorted((ROOT / 'recomp').glob('labels-*.toml')):
        text = path.read_text(encoding='utf-8').replace('\r\n', '\n')
        for block in text.split('[[label]]')[1:]:
            fields: dict[str, str] = {}
            for line in block.split('\n'):
                key, sep, value = line.partition('=')
                if not sep or key.strip() not in ('addr', 'bank', 'name', 'note'):
                    continue
                value = value.strip()
                if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
                    value = value[1:-1].replace('\\"', '"').replace('\\\\', '\\')
                fields[key.strip()] = value
            if 'addr' not in fields or 'name' not in fields:
                continue
            key = (int(fields.get('bank', '0'), 0), int(fields['addr'], 16))
            entry = (fields['name'], fields.get('note', ''))
            if key in labels and labels[key] != entry:
                raise SystemExit(
                    f'apply_labels: conflicting labels for bank ${key[0]:02x}:{key[1]:04x}')
            labels[key] = entry
    return labels


def parse_block(block: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in block.split('\n'):
        key, sep, value = line.partition('=')
        if sep and key.strip() in FIELDS:
            fields[key.strip()] = value.strip().strip('"')
    return fields


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='report only, do not write')
    ap.add_argument('--symbols', default='recomp/symbols.toml')
    args = ap.parse_args()

    labels = read_labels()
    if not labels:
        raise SystemExit('apply_labels: no recomp/labels-*.toml found')

    path = ROOT / args.symbols
    if not path.exists():
        raise SystemExit(f'apply_labels: {path} missing — run tools/gen_roots.py first')
    text = path.read_text(encoding='utf-8').replace('\r\n', '\n')

    parts = text.split('[[func]]')
    head, blocks = parts[0], parts[1:]
    if not blocks:
        raise SystemExit('apply_labels: no [[func]] blocks — run tools/gen_roots.py first')

    renamed = 0
    already = 0
    per_bank: dict[int, int] = {}
    out_blocks: list[str] = []

    # Names already claimed by a hand-written or previously renamed block.
    used_names: set[str] = set()
    for block in blocks:
        existing = parse_block(block).get('name', '')
        if existing and not ADDRESS_NAME.match(existing):
            used_names.add(existing)

    for block in blocks:
        fields = parse_block(block)
        try:
            key = (int(fields.get('bank', '0'), 0), int(fields['addr'], 16))
        except (KeyError, ValueError):
            out_blocks.append(block)
            continue
        if key not in labels:
            out_blocks.append(block)
            continue

        new_name, prose = labels[key]
        if not ADDRESS_NAME.match(fields.get('name', '')):
            already += 1
            out_blocks.append(block)
            continue
        if new_name in used_names:
            new_name = f'{new_name}_{key[0]:02x}'
        used_names.add(new_name)

        lines = block.split('\n')
        for i, line in enumerate(lines):
            key_name, sep, _ = line.partition('=')
            if not sep:
                continue
            key_name = key_name.strip()
            if key_name == 'name':
                lines[i] = f'name = "{new_name}"'
            elif key_name == 'note':
                old = fields.get('note', '')
                text = f'{old}; C3 compendium: {prose}' if old else f'C3 compendium: {prose}'
                escaped = text.replace('\\', '\\\\').replace('"', '\\"')
                lines[i] = f'note = "{escaped}"'
        renamed += 1
        per_bank[key[0]] = per_bank.get(key[0], 0) + 1
        out_blocks.append('\n'.join(lines))

    body = head + ''.join('[[func]]' + b for b in out_blocks)

    seen: dict[str, int] = {}
    for m in re.finditer(r'^name = "([^"]+)"', body, re.M):
        seen[m.group(1)] = seen.get(m.group(1), 0) + 1
    dupes = sorted(n for n, c in seen.items() if c > 1)
    if dupes:
        raise SystemExit(f'apply_labels: duplicate names after rename: {dupes[:5]}')

    summary = ', '.join(f'${b:02X}:{c}' for b, c in sorted(per_bank.items())) or 'none'
    print(f'{renamed} roots renamed ({summary}); {already} already specific; '
          f'{len(labels)} labels available')
    if args.check:
        print('check only — symbols.toml not written')
        return 0
    path.write_text(body, encoding='utf-8', newline='\n')
    try:
        import tomllib
        tomllib.loads(path.read_text(encoding='utf-8'))
    except ImportError:
        pass
    except Exception as exc:                      # noqa: BLE001 - report, do not hide
        raise SystemExit(f'apply_labels: wrote unparsable TOML: {exc}')
    print(f'wrote {path} (parses as TOML)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
