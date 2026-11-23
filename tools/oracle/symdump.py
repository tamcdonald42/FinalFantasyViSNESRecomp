#!/usr/bin/env python3
"""Symbolize a Windows minidump written by the FF6 recomp runner.

    python symdump.py <crash.dmp> <exe> [<pdb-or-exe-for-symbols>]

Reads the exception stream for the faulting address, resolves it against the
module list to get an RVA, then scans the faulting thread's stack for
module-pointing values and symbolizes all of them through llvm-symbolizer.

Why this exists: the Release build carries no /Zi, so crash reports say
"last recomp func: (none)" and the faulting address is anonymous. A
RelWithDebInfo build reproduces the same fault and, with the PDB, names the
function and line. See STATUS.md Step 17.
"""
import struct
import subprocess
import sys

MDMP_SIGNATURE = 0x504D444D
STREAM_THREAD_LIST = 3
STREAM_MODULE_LIST = 4
STREAM_EXCEPTION = 6

LLVM_SYMBOLIZER = (r"C:/Tools/clang+llvm-23.1.5-x86_64-pc-windows-msvc/"
                   r"bin/llvm-symbolizer.exe")


def read_dump(path):
    data = open(path, 'rb').read()
    sig, ver, nstreams, dir_rva = struct.unpack_from('<IIII', data, 0)
    if sig != MDMP_SIGNATURE:
        raise SystemExit(f"not a minidump: {path}")
    streams = {}
    for i in range(nstreams):
        off = dir_rva + i * 12
        stype, size, rva = struct.unpack_from('<III', data, off)
        streams.setdefault(stype, []).append((size, rva))
    return data, streams


def parse_modules(data, rva):
    count = struct.unpack_from('<I', data, rva)[0]
    mods = []
    for i in range(count):
        off = rva + 4 + i * 108
        base, size_img = struct.unpack_from('<QI', data, off)
        name_rva = struct.unpack_from('<I', data, off + 20)[0]
        nlen = struct.unpack_from('<I', data, name_rva)[0]
        name = data[name_rva + 4:name_rva + 4 + nlen].decode('utf-16-le', 'replace')
        mods.append((base, size_img, name))
    return mods


def parse_exception(data, rva):
    tid, _align = struct.unpack_from('<II', data, rva)
    code, flags = struct.unpack_from('<II', data, rva + 8)
    addr = struct.unpack_from('<Q', data, rva + 24)[0]
    nparams = struct.unpack_from('<I', data, rva + 32)[0]
    params = struct.unpack_from('<%dQ' % min(nparams, 15), data, rva + 40)
    ctx_size, ctx_rva = struct.unpack_from('<II', data, rva + 160)
    return dict(thread=tid, code=code, flags=flags, addr=addr,
                params=params, ctx=(ctx_size, ctx_rva))


def ctx_regs(data, ctx_rva):
    # CONTEXT_AMD64: Rax 0x78 ... Rsp 0x98, Rbp 0xA0, Rip 0xF8
    names = {'Rax': 0x78, 'Rcx': 0x80, 'Rdx': 0x88, 'Rbx': 0x90, 'Rsp': 0x98,
             'Rbp': 0xA0, 'Rsi': 0xA8, 'Rdi': 0xB0, 'R8': 0xB8, 'R9': 0xC0,
             'R10': 0xC8, 'R11': 0xD0, 'R12': 0xD8, 'R13': 0xE0, 'R14': 0xE8,
             'R15': 0xF0, 'Rip': 0xF8}
    return {n: struct.unpack_from('<Q', data, ctx_rva + o)[0] for n, o in names.items()}


def thread_stack(data, streams, want_tid):
    size, rva = streams[STREAM_THREAD_LIST][0]
    count = struct.unpack_from('<I', data, rva)[0]
    for i in range(count):
        off = rva + 4 + i * 48
        tid = struct.unpack_from('<I', data, off)[0]
        if tid != want_tid:
            continue
        # MINIDUMP_MEMORY_DESCRIPTOR: StartOfMemoryRange u64, then (DataSize u32, Rva u32)
        start = struct.unpack_from('<Q', data, off + 16)[0]
        msize, mrva = struct.unpack_from('<II', data, off + 24)
        return start, data[mrva:mrva + msize]
    return None, None


def symbolize(exe, pdb, rvas):
    if not rvas:
        return {}
    args = [LLVM_SYMBOLIZER, '--obj=' + exe, '--relative-address',
            '--functions=linkage', '--demangle', '--inlining']
    if pdb:
        args.append('--pdb=' + pdb)
    inp = '\n'.join('0x%X' % r for r in rvas) + '\n'
    out = subprocess.run(args, input=inp, capture_output=True, text=True)
    lines = out.stdout.splitlines()
    res = {}
    for i, r in enumerate(rvas):
        chunk = []
        for ln in lines[i * 3:i * 3 + 2]:
            if ln.strip():
                chunk.append(ln.strip())
        res[r] = ' | '.join(chunk) if chunk else out.stderr.strip()[:120]
    return res


def main():
    dmp, exe = sys.argv[1], sys.argv[2]
    pdb = sys.argv[3] if len(sys.argv) > 3 else ''
    data, streams = read_dump(dmp)
    mods = parse_modules(data, streams[STREAM_MODULE_LIST][0][1])
    exc = parse_exception(data, streams[STREAM_EXCEPTION][0][1])
    regs = ctx_regs(data, exc['ctx'][1])

    print("modules:")
    for base, size, name in mods:
        print(f"  0x{base:016X} +0x{size:08X}  {name.split(chr(92))[-1]}")
    fault = exc['addr']
    host = next((m for m in mods if m[0] <= fault < m[0] + m[1]), None)
    print(f"\nexception 0x{exc['code']:08X}  params={[hex(p) for p in exc['params']]}")
    print(f"fault address 0x{fault:016X}")
    if not host:
        print("fault is outside every module image (garbage pointer)")
        return
    rva = fault - host[0]
    print(f"inside {host[2].split(chr(92))[-1]} at RVA 0x{rva:X}")

    print("\nregisters at fault:")
    for n in ('Rax', 'Rbx', 'Rcx', 'Rdx', 'Rsi', 'Rdi', 'Rsp', 'Rbp', 'Rip'):
        print(f"  {n}=0x{regs[n]:016X}")

    # candidate return addresses from the faulting thread's stack
    start, stack = thread_stack(data, streams, exc['thread'])
    cands = []
    if stack:
        base, size = host[0], host[1]
        for off in range(0, len(stack) - 8, 8):
            v = struct.unpack_from('<Q', stack, off)[0]
            if base <= v < base + size:
                cands.append((start + off, v - base))
        print(f"\nstack: {len(stack)} bytes at 0x{start:X}, "
              f"{len(cands)} code-pointer candidates")

    rvas = [rva] + [r for _, r in cands[:24]]
    syms = symbolize(exe, pdb, rvas)
    print("\nfault site:")
    print("  ", syms.get(rva, '(no symbol)'))
    if cands:
        print("\ncall chain candidates (nearest stack first):")
        seen = set()
        for addr, r in cands[:24]:
            s = syms.get(r, '')
            key = s.split(' | ')[0]
            if key in seen:
                continue
            seen.add(key)
            print(f"  0x{r:08X}  {s[:150]}")


if __name__ == '__main__':
    main()
