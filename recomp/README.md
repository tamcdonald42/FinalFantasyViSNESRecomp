# recomp/

Analysis input for Final Fantasy VI. `tools/regen.sh` reads this directory and
writes `src/gen/`.

| File | Owner | Notes |
|---|---|---|
| `bank*.cfg` | you, plus generated blocks | Per-bank analysis directives |
| `symbols.toml` | you | Progressive name map; the source of truth |
| `funcs.h` | generated | Re-synced by `tools/regen.sh` — do not hand-edit |
| `labels-*.toml` | `tools/import_labels.py` | Block names from an external disassembly — generated locally, gitignored |

The block between `>>> BEGIN symbols.toml` and `<<< END symbols.toml` in
each bank cfg is rewritten from `symbols.toml` on every regen. Edit
`symbols.toml`, not the block.

Python 3.11+ includes the TOML reader. On older Python, install it with
`python -m pip install tomli`. Projects without `symbols.toml` do not need it.

Each `[[func]]` has a C identifier `name`, a 16-bit hexadecimal `addr`
(for example, `"8000"`), and an integer `bank` (for example, `0xc0`).
Missing bank configs are created automatically; hand-written directives outside
the generated blocks are preserved. Removing an entry also removes its generated
directives on the next regen.

- `emit = false` (the default) writes a `symbol` label and `force_lle` boundary:
  it stays interpreted, with no generated `func` declaration in `funcs.h`.
- `emit = true` writes a `func` declaration and requests analysis from that
  entry, even without `--cfg-roots`. Only variants the analyzer proves safe
  become AOT code; others still use the interpreter. Optional `entry_m` and
  `entry_x` select the initial register-width flags (0 or 1, both default to 1).

## Names

`symbols.toml` is composed by `tools/gen_roots.py`, and every root in it is
named after its address (`sub_C30138`). `labels-*.toml` maps addresses to the
names a human wrote for those blocks; `tools/import_labels.py` regenerates one
from the source disassembly text, and only imports an address that is already
a known function entry, so it cannot invent a root or name the middle of an
instruction.

`labels-c3.toml` is generated, not shipped: it is an extract of the **Final
Fantasy VI Bank C3 Compendium** by Novalia Spirit (Jame Coban), 2016, whose terms
reserve reproduction of the document's contents, so the file is gitignored and
built locally from your own copy via `import_labels.py --disasm`. See
`README.md`, "Credits".

`tools/apply_labels.py` merges the names into `symbols.toml` after gen_roots
and before regen:

```sh
python3 tools/gen_roots.py --done C0,C1,C2,C3,EE --bank none
python3 tools/apply_labels.py
tools/regen.sh --rom <rom>
```

It rewrites `name` and appends the prose to `note`; `emit`, `entry_mx` and the
root set are untouched, so the AOT set does not move. Generated C then differs
only in identifiers and in the alphabetical order of each translation unit's
forward-declaration block. The pass is idempotent — a block whose name is
already specific is never renamed again — and it validates the TOML it writes.

An unchanged cfg file is not by itself evidence of failed code generation:
`src/gen/bank*_v2.c` contains code discovered from the ROM, while `bank*.cfg`
contains analysis inputs. Discovery does not copy every reached function back
into the configs.
