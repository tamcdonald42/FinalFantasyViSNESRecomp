"""ca65/.dbg source/address classifier for `export_disassembly_authority.py`.

The framework's exporter knows three reference formats (asar, SMWDisX, usdasm).
This module plugs the *generic 65816* path (`--format asar65816`) with a ca65
backend, so `everything8215/ff6` can serve as Final Fantasy VI's analysis
authority. Nothing here decodes instructions: lengths come from the linker's own
span records, which is the property the exporter is built around.

Input is cc65's debug file (`ld65 --dbgfile`), which already carries what the
exporter needs:

    seg  id=6,name="field_code",start=0xC00000,size=0xD613,...,ooffs=0
    span id=0,seg=6,start=0,size=3
    line id=1,file=67,line=409,span=24443
    file id=0,name="src/field/field_main.asm"

`seg.start` is an absolute CPU address, `span.start` is segment-relative, and a
`line` record's `span=N` links a source line to a span, so `pc24 = seg.start +
span.start` and the span's own `size` is the length.

Three behaviours matter for the emitted authority:

1. **Every span in a ROM segment becomes a row**, not just the ones a line
   record names. The exporter derives a row's length as "distance to the next
   row", so leaving unnamed spans out would inflate its neighbours into bogus
   multi-byte spans — and an inflated *data* row is silently wrong, because data
   regions are hard code exclusions.
2. **An anonymous span inherits the line of the span containing it.** cc65
   records a line for the statement that emitted code and none for the bytes a
   macro expanded into; without inheritance every macro expansion is an
   unconstrained hole.
3. **Macros are classified once, not guessed per invocation.** ff6 writes whole
   languages as macros: `shorta0`/`clr_ax` expand to instructions, while
   `obj_script`/`ptr_tbl`/`move` expand to script or table *bytes*. A macro whose
   expansion is only instructions is code, one whose expansion is only
   data-emitting directives is data, and anything conditional stays unknown.
   Classification is a fixpoint over the macro graph, because bodies invoke
   other macros — the missing piece on the first attempt, since the sources use
   ca65's `.mac`/`.endmac` abbreviations and my definition regexes only knew
   `.macro`/`.endmacro`, so nearly every invocation fell through as unknown.

Interface (what `export_disassembly_authority.py` calls):

    parse_symbols(path)              -> (labels, files, rows)
    resolve_source_files(ref, files) -> (paths, lines)
    find_macros(lines)               -> (macros, owners)
    strip_label / strip_comment / head_word / statement_kind / REP_PREFIX_RE
"""
from __future__ import annotations

import re
from pathlib import Path

# A line the debug file has no source record for (generated data, incbin,
# .res padding, an inter-span gap). Kept well outside cc65's own file ids.
PSEUDO_FILE_ID = -1

_65816 = {
    "ADC", "AND", "ASL", "BCC", "BCS", "BEQ", "BIT", "BMI", "BNE", "BPL",
    "BRA", "BRK", "BRL", "BVC", "BVS", "CLC", "CLD", "CLI", "CLV", "CMP",
    "COP", "CPX", "CPY", "DEC", "DEX", "DEY", "EOR", "INC", "INX", "INY",
    "JML", "JMP", "JSL", "JSR", "LDA", "LDX", "LDY", "LSR", "MVN", "MVP",
    "NOP", "ORA", "PEA", "PEI", "PER", "PHA", "PHB", "PHD", "PHK", "PHP",
    "PHX", "PHY", "PLA", "PLB", "PLD", "PLP", "PLX", "PLY", "REP", "ROL",
    "ROR", "RTI", "RTL", "RTS", "SBC", "SEC", "SED", "SEI", "SEP", "STA",
    "STP", "STX", "STY", "STZ", "TAX", "TAY", "TCD", "TCS", "TDC", "TRB",
    "TSB", "TSC", "TSX", "TXA", "TXS", "TXY", "TYA", "TYX", "WAI", "WDM",
    "XBA", "XCE",
}

# Directives that emit bytes with no instruction semantics of their own.
_DATA_DIRECTIVES = {
    "DB", "DW", "DL", "DD", "BYTE", "WORD", "DWORD", "ADDR", "FARADDR",
    "INCBIN", "RES", "ALIGN", "DS", "DSB", "DCB", "FILL", "DSTRUCT",
}

# Directives that neither emit bytes nor say anything about kind.
_NEUTRAL_DIRECTIVES = {
    "IF", "IFDEF", "IFNDEF", "IFBLANK", "IFNBLANK", "IFCONST", "IFREF",
    "ELSE", "ELSEIF", "ENDIF", "REPEAT", "ENDREP", "LOCAL", "SCOPE",
    "ENDSCOPE", "PROC", "ENDPROC", "DEFINE", "UNDEFINE", "SET", "ENUM",
    "ENDENUM", "ERROR", "FATAL", "OUT", "WARNING", "ASSERT", "IMPORT",
    "IMPORTZP", "EXPORT", "EXPORTZP", "GLOBAL", "GLOBALZP", "SEGMENT",
    "ZEROPAGE", "FEATURE", "LIST", "PUSHSEG", "POPSEG", "CONSTRUCTOR",
    "DESTRUCTOR", "INTERRUPTOR", "MACPACK", "ORG", "FILEOPT", "LINEINFO",
    "STRUCT", "ENDSTRUCT", "TAG", "UNION", "ENDUNION", "CHARMAP", "DEBUGINFO",
}

_LABEL_RE = re.compile(r"^\s*(?:[A-Za-z_@][\w@.]*:|@[\w@]+:)\s*")
_WORD_RE = re.compile(r"^\s*([A-Za-z_.][\w.]*)")
_ASSIGN_RE = re.compile(
    r"^\s*:?:?[A-Za-z_][\w@]*(?:::[A-Za-z_]\w*)?\s*(?::=|=|\s\.set\s)", re.I)
_MACRO_DEF_RE = re.compile(r"^\s*\.(?:macro|mac)\s+([A-Za-z_]\w*)", re.I)
_MACRO_END_RE = re.compile(r"^\s*\.(?:endmacro|endmac)\b", re.I)
_DEFINE_RE = re.compile(r"^\s*\.define\s+([A-Za-z_]\w*)\s*(?:\([^)]*\))?\s*(.*)$", re.I)

# Populated by find_macros(); statement_kind() consults them.
_CODE_MACROS: set[str] = set()
_DATA_MACROS: set[str] = set()
# Macro parameter names (lower-case). A bare parameter is neither code nor data.
_PARAMS: set[str] = set()

# Deliberately never matches: with real span sizes from the linker there is no
# "one source line owns N same-width instructions" case to special-case, and the
# exporter's repeat handling would force size 1 on a 2-byte REP/SEP.
REP_PREFIX_RE = re.compile(r"(?!)")


def strip_comment(text: str) -> str:
    return text.split(";", 1)[0]


def strip_label(text: str) -> str:
    return _LABEL_RE.sub("", text, count=1)


def head_word(text: str) -> str:
    m = _WORD_RE.match(strip_label(text))
    return m.group(1).upper() if m else ""


def _statement_class(text: str, know: dict[str, str]):
    """'code' / 'data' / 'none' (emits nothing) / 'unknown' for one source line."""
    line = strip_comment(text)
    if _ASSIGN_RE.match(line):
        # `name = expr` / `::name := expr` computes a symbol; it emits nothing.
        return "none"
    word = head_word(line)
    if not word:
        return "none"
    if word.startswith("."):
        core = word[1:]
        if core in _DATA_DIRECTIVES:
            return "data"
        return "none"
    if word in _65816:
        return "code"
    if word.lower() in _PARAMS:
        # A macro parameter: whatever the caller passed (`op_n op, n` takes the
        # instruction as an argument). Neutral, so the rest of the body decides.
        return "none"
    known = know.get(word.lower())
    if known in (None, "unknown", "none"):
        # A macro call whose parameter carries the emitted directive, e.g.
        # `_ptr_tbl .addr, label, count`: the argument says what gets emitted.
        for token in strip_label(line).split()[1:]:
            token = token.lstrip(",(").rstrip(",)")
            bare = token[1:].upper() if token.startswith(".") else ""
            if bare in _DATA_DIRECTIVES:
                return "data"
    return known or "unknown"


def statement_kind(text: str, arch: str = "65816", *_unused) -> str:
    word = head_word(strip_label(strip_comment(text)))
    if not word:
        return "unknown"
    core = word.lstrip(".")
    if core in _65816:
        return "code"
    if core in _DATA_DIRECTIVES:
        return "data"
    if word.lower() in _DATA_MACROS:
        return "data"
    if word.lower() in _CODE_MACROS:
        return "code"
    return "unknown"


class Macro:
    __slots__ = ("name", "file_id", "first", "last")

    def __init__(self, name, file_id, first, last):
        self.name, self.file_id, self.first, self.last = name, file_id, first, last


def _fields(record: str) -> dict:
    """`k=v,k="quoted",...` -> dict, without splitting inside quotes."""
    out = {}
    i, n = 0, len(record)
    while i < n:
        while i < n and record[i] in ", \t\r\n":
            i += 1
        j = record.find("=", i)
        if j < 0:
            break
        key = record[i:j].strip()
        i = j + 1
        if i < n and record[i] == '"':
            i += 1
            buf = []
            while i < n and record[i] != '"':
                if record[i] == "\\" and i + 1 < n:
                    buf.append(record[i + 1])
                    i += 2
                    continue
                buf.append(record[i])
                i += 1
            out[key] = "".join(buf)
            while i < n and record[i] != ",":
                i += 1
        else:
            start = i
            while i < n and record[i] not in ", \t\r\n":
                i += 1
            out[key] = record[start:i]
    return out


def parse_symbols(path):
    """Parse a cc65 .dbg into (labels, files, rows).

    rows maps an absolute CPU address to (file_id, 1-based source line).
    """
    segs = {}          # sid -> (start, size, has_rom_output)
    spans = {}         # span_id -> (seg_id, start, size)
    line_of_span = {}  # span_id -> (fid, lineno)
    files = {}

    with open(path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            if raw.startswith("seg\t"):
                f = _fields(raw[4:])
                sid = int(f["id"], 0)
                segs[sid] = (
                    int(f.get("start", "0"), 0),
                    int(f.get("size", "0"), 0),
                    f.get("ooffs") not in (None, ""),
                )
            elif raw.startswith("span\t"):
                f = _fields(raw[5:])
                spans[int(f["id"], 0)] = (
                    int(f["seg"], 0), int(f["start"], 0), int(f["size"], 0),
                )
            elif raw.startswith("line\t"):
                f = _fields(raw[5:])
                sid = f.get("span")
                if sid is not None:
                    # `span=` may be a `+`-joined list when one source line owns
                    # several spans (macro expansion): the first is its origin.
                    line_of_span.setdefault(
                        int(sid.split("+", 1)[0], 0),
                        (int(f["file"], 0), int(f["line"], 0)),
                    )
            elif raw.startswith("file\t"):
                f = _fields(raw[5:])
                files[int(f["id"], 0)] = f.get("name", "")

    # Group spans per ROM segment, then materialise rows in address order.
    per_seg = {}
    for span_id, (sid, start, size) in spans.items():
        if size <= 0 or sid not in segs:
            continue
        seg_start, _seg_size, rom = segs[sid]
        if not rom:
            continue
        per_seg.setdefault(sid, []).append((seg_start + start, size, span_id))

    rows = {}
    for sid, items in per_seg.items():
        items.sort(key=lambda it: (it[0], -it[1]))
        stack = []  # (end, fid, lineno) of open named spans, ascending start
        resolved = {}
        for addr, size, span_id in items:
            while stack and stack[-1][0] <= addr:
                stack.pop()
            hit = line_of_span.get(span_id)
            if hit is not None:
                stack.append((addr + size, hit[0], hit[1]))
                resolved[span_id] = hit
            else:
                inherited = next((e for e in reversed(stack) if e[0] >= addr + size), None)
                resolved[span_id] = (inherited[1], inherited[2]) if inherited else (PSEUDO_FILE_ID, 0)

        cursor = None
        for addr, size, span_id in sorted(items):
            if cursor is not None and addr > cursor:
                rows[cursor] = (PSEUDO_FILE_ID, 0)          # gap: unknown
            elif cursor is not None and addr < cursor:
                continue                                     # overlap: first wins
            rows[addr] = resolved[span_id]
            cursor = addr + size
        seg_start, seg_size, _ = segs[sid]
        if cursor is not None and seg_size and cursor < seg_start + seg_size:
            rows[cursor] = (PSEUDO_FILE_ID, 0)               # trailing fill

    files[PSEUDO_FILE_ID] = "<generated>"
    return {}, files, rows


def resolve_source_files(ref, files):
    ref = Path(ref)
    paths, lines = {}, {}
    for fid, name in files.items():
        clean = name[2:] if name.startswith("./") else name
        candidate = ref / clean
        paths[fid] = candidate
        try:
            lines[fid] = candidate.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            lines[fid] = []
    return paths, lines


def find_macros(lines):
    """Classify ca65 macros and `.define` aliases, then return the exporter's pair.

    `macros`/`owners` are only for macros whose expansion stays unknown; the
    classified ones are consulted by statement_kind() instead, because their
    expansion kind is settled.
    """
    macros = {}
    define_body = {}
    for fid, body in lines.items():
        open_at = None
        name = None
        for idx, text in enumerate(body, 1):
            if open_at is None:
                m = _MACRO_DEF_RE.match(text)
                if m:
                    open_at, name = idx, m.group(1)
                    rest = strip_comment(text)[m.end():]
                    for param in rest.replace("(", ",").split(","):
                        param = param.strip()
                        if param:
                            _PARAMS.add(param.lower())
                    continue
                d = _DEFINE_RE.match(strip_comment(text))
                if d:
                    define_body[d.group(1).lower()] = d.group(2).strip()
            elif _MACRO_END_RE.match(text):
                macros[name] = Macro(name, fid, open_at, idx)
                open_at = name = None

    bodies = {name: lines[m.file_id][m.first:m.last - 1] for name, m in macros.items()}
    know: dict[str, str] = {}
    for _ in range(8):
        changed = False
        for name, body in bodies.items():
            kinds = {_statement_class(t, know) for t in body}
            kinds.discard("none")
            kind = ("code" if kinds == {"code"} else
                    "data" if kinds == {"data"} else
                    "none" if not kinds else "unknown")
            if know.get(name.lower()) != kind:
                know[name.lower()] = kind
                changed = True
        for name, text in define_body.items():
            kind = _statement_class(text, know)
            if know.get(name.lower()) != kind:
                know[name.lower()] = kind
                changed = True
        if not changed:
            break

    _CODE_MACROS.clear()
    _DATA_MACROS.clear()
    for name, kind in know.items():
        if kind == "code":
            _CODE_MACROS.add(name)
        elif kind == "data":
            _DATA_MACROS.add(name)

    macro_names = {k.lower() for k in macros}
    owners = {}
    for fid, body in lines.items():
        for idx, text in enumerate(body, 1):
            word = head_word(strip_label(strip_comment(text))).lower()
            if word in macro_names and know.get(word) == "unknown":
                # keys into `macros`, which is what the exporter indexes with.
                owners[(fid, idx)] = next(k for k in macros if k.lower() == word)
    return macros, owners
