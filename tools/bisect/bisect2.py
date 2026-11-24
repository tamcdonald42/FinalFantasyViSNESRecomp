#!/usr/bin/env python3
"""Signature-aware bisection of the bank-C0 roots.

The plain bisect assumed "a failing subset stays failing when narrowed". It does
not: promoting the first 133 roots blanks the screen entirely, while the second
134 reproduce the mild divergence of the full set, and any single root is clean.
So class the outcome and only narrow towards the signature we are chasing:

  PASS   no frame differs
  MILD   some frames differ, first difference after the first dumped frame
  BLACK  every frame differs from the first one (screen never renders)

Recurse into whichever half is MILD; stop when neither half is.
"""
import os
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
AUTH = REPO / "_authority"
BASH = os.environ.get("BASH", "bash")
TRIAL = r"C:/Temp/trial.sh"
LOG = pathlib.Path(os.environ.get("BISECT_LOG", "bisect2.log"))
FIRST_DUMPED = "frame_000480.bmp"


def log(msg):
    print(msg, flush=True)
    with open(LOG, "a", encoding="utf-8") as fh:
        fh.write(msg + "\n")


def load_blocks(path):
    text = pathlib.Path(path).read_text(encoding="utf-8")
    out = []
    for chunk in text.split("[[func]]"):
        chunk = chunk.strip()
        if not chunk or chunk.startswith("#"):
            continue
        out.append("[[func]]\n" + chunk)
    return out


def name_of(block):
    m = re.search(r'name\s*=\s*"([^"]+)"', block)
    a = re.search(r'addr\s*=\s*"([^"]+)"', block)
    return f"C0:{a.group(1) if a else '?'} {m.group(1) if m else '?'}"


def trial(idxs, blocks, tag):
    frag = AUTH / f"frag2_{tag}.toml"
    frag.write_text("\n\n".join(blocks[i] for i in idxs) + "\n", encoding="utf-8")
    cp = subprocess.run([BASH, TRIAL, str(frag).replace("\\", "/")],
                        capture_output=True, text=True)
    out = (cp.stdout or "") + (cp.stderr or "")
    line = [ln for ln in out.splitlines() if ln.startswith("TRIAL=")]
    if not line:
        log(f"[{tag}] no result: {out[-500:]}")
        return "ERROR"
    ndiff = int(re.search(r"ndiff=(\d+)", line[-1]).group(1))
    first = re.search(r"first=(\S*)", line[-1]).group(1)
    kind = "PASS" if ndiff == 0 else ("BLACK" if first == FIRST_DUMPED else "MILD")
    log(f"[{tag}] {len(idxs)} roots -> {kind} (ndiff={ndiff} first={first or '-'})")
    return kind


def main():
    blocks = load_blocks(AUTH / "fragB.toml")     # second half: reproduces the mild signature
    log(f"=== bisect2: {len(blocks)} candidate roots (half B) ===")
    cand = list(range(len(blocks)))
    kind = trial(cand, blocks, "pool")
    if kind != "MILD":
        log(f"pool is {kind}, not MILD — nothing to narrow")
        return 0
    step = 0
    while len(cand) > 1:
        step += 1
        half = len(cand) // 2
        a, b = cand[:half], cand[half:]
        ka = trial(a, blocks, f"L{step}a")
        if ka == "MILD":
            cand = a
            log(f"level {step}: first half MILD -> {len(cand)}")
            continue
        kb = trial(b, blocks, f"L{step}b")
        if kb == "MILD":
            cand = b
            log(f"level {step}: second half MILD -> {len(cand)}")
            continue
        log(f"level {step}: neither half is MILD ({ka}/{kb}) — interaction; stopping")
        break
    log("=== minimal MILD candidate set ===")
    for i in cand:
        log("  " + name_of(blocks[i]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
