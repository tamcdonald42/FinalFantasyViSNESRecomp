#!/usr/bin/env python3
"""Bisect the bank-C0 root set against the frame-identical baseline.

Each trial promotes a subset, regenerates, rebuilds, replays frames 480-670 and
byte-compares them with the capture taken before any AOT promotion. A subset
"fails" when any frame differs.
"""
import os
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
BASH = os.environ.get("BASH", "bash")
TRIAL = os.environ.get("TRIAL_SCRIPT", str(REPO / "tools" / "bisect" / "trial.sh"))
LOG = pathlib.Path(os.environ.get("BISECT_LOG", "bisect_out.log"))


def log(msg):
    print(msg, flush=True)
    with open(LOG, "a", encoding="utf-8") as fh:
        fh.write(msg + "\n")


def load_blocks():
    text = (REPO / "_authority" / "roots.toml").read_text(encoding="utf-8")
    blocks = []
    for chunk in text.split("[[func]]"):
        chunk = chunk.strip()
        if not chunk or chunk.startswith("#"):
            continue
        blocks.append("[[func]]\n" + chunk)
    return blocks


def name_of(block):
    m = re.search(r'name\s*=\s*"([^"]+)"', block)
    addr = re.search(r'addr\s*=\s*"([^"]+)"', block)
    bank = re.search(r"bank\s*=\s*(0x[0-9A-Fa-f]+)", block)
    return f"{bank.group(1) if bank else '?'}:{addr.group(1) if addr else '?'} {m.group(1) if m else '?'}"


def trial(idxs, blocks, tag):
    frag = REPO / "_authority" / f"frag_{tag}.toml"
    frag.write_text("\n\n".join(blocks[i] for i in idxs) + "\n", encoding="utf-8")
    cp = subprocess.run([BASH, TRIAL, str(frag).replace("\\", "/")],
                        capture_output=True, text=True)
    out = (cp.stdout or "") + (cp.stderr or "")
    m = re.search(r"ndiff=(\d+)", out)
    if not m:
        log(f"[{tag}] no result:\n{out[-800:]}")
        return None, out
    ndiff = int(m.group(1))
    line = [ln for ln in out.splitlines() if ln.startswith("TRIAL=")][-1]
    log(f"[{tag}] {len(idxs)} roots -> {line}")
    return ndiff == 0, out


def main():
    blocks = load_blocks()
    log(f"=== bisect start: {len(blocks)} root blocks ===")
    cand = list(range(len(blocks)))

    # Sanity: with no promoted roots the tree must reproduce the baseline
    # exactly. If this fails the harness (window, script, dump) is wrong.
    ok0, _ = trial([], blocks, "empty")
    if ok0 is not True:
        log("harness sanity check FAILED (no roots promoted but frames differ)")
        return 1

    # Confirm the baseline premise once: the full set must fail.
    ok, _ = trial(cand, blocks, "all")
    if ok:
        log("full set passes now — nothing to bisect")
        return 0

    step = 0
    while len(cand) > 1:
        half = len(cand) // 2
        a, b = cand[:half], cand[half:]
        step += 1
        ok_a, _ = trial(a, blocks, f"s{step}a")
        if ok_a is not None and not ok_a:
            cand = a
            log(f"step {step}: first half fails -> {len(cand)} candidates")
            continue
        ok_b, _ = trial(b, blocks, f"s{step}b")
        if ok_b is not None and not ok_b:
            cand = b
            log(f"step {step}: second half fails -> {len(cand)} candidates")
            continue
        log(f"step {step}: both halves pass — interaction between halves; stopping")
        break

    log("=== candidate culprits ===")
    for i in cand:
        log("  " + name_of(blocks[i]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
