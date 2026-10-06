#!/usr/bin/env python3
"""Every header must include the standard headers its own declarations need.

WHY THIS EXISTS. On Tuesday 6 October 2026 a struct carrying a `bool` went into
c/instr/freqcount.h, which included <stdint.h> and not <stdbool.h>. Every check
on the authoring laptop passed, because none of them compiles anything, and gcc
15 in WSL on the build laptop refused the file one push later. The fix was one
line. The round trip was not.

A HEADER HAS TO BE SELF CONTAINED, which is the rule this enforces: if the text
of a header uses a name that a standard header provides, that header must
include it DIRECTLY rather than inheriting it from whatever the including file
happened to pull in first. freqcount.c compiled before this struct existed
because board.h reached <stdbool.h> on its way through, which is luck and not a
guarantee: the moment anything includes freqcount.h first, the luck ends.

.c FILES ARE CHECKED MORE LENIENTLY, for a reason rather than for convenience. A
translation unit may legitimately rely on its own header to supply a type, since
that header is the first thing it includes, so for .c files a name is satisfied
by a direct include OR by any first-party header it includes that satisfies it.
A header, included by someone else in an order it cannot see, gets no such
latitude.

C ONLY, AND NOT C++, which is a correctness decision and not a convenience. In
C++ `bool`, `true` and `false` are keywords and need no header at all, and the
sized integers arrive through <cstdint> under names this check does not model.
Including the .cpp and .hpp files produced thirty-odd findings, every one of them
wrong. The bug class this guards is specific to C, so the scope is too.

HOW A LOCAL INCLUDE IS RESOLVED, because the first version of this got it wrong
and reported three false findings. `#include "board.h"` is not relative to the
including file here; CMake supplies the directory. So a quoted include is looked
for next to the file first and then by unique basename anywhere in the tree. A
basename that is not unique is left unresolved and reported as such rather than
guessed at, because guessing is how the false findings happened.

WHAT IT CANNOT DO. It reads text, so it is not a compiler. It will not catch a
missing declaration, a wrong signature, or a type used through a typedef it
cannot follow. It catches exactly one class, the class that cost a round trip,
and the file that reports it says so rather than claiming more.

Comments and string literals are stripped before the search, so the word bool in
a sentence about bools does not satisfy or trigger anything.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
INCLUDE_SYS = re.compile(r'^\s*#\s*include\s*<([^>]+)>', re.M)
INCLUDE_LOCAL = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)

# name pattern -> the standard header that provides it
NEEDS = [
    (re.compile(r"\bbool\b|\btrue\b|\bfalse\b"), "stdbool.h"),
    (re.compile(r"\b(?:u?int(?:8|16|32|64)_t|u?intptr_t|u?intmax_t)\b"), "stdint.h"),
    (re.compile(r"\bsize_t\b|\bptrdiff_t\b|\bNULL\b|\boffsetof\b"), "stddef.h"),
    (re.compile(r"\bva_list\b|\bva_start\b|\bva_arg\b|\bva_end\b"), "stdarg.h"),
]

# stddef's names also arrive with any of these, which is legitimate and common
ALSO_PROVIDES = {
    "stddef.h": {"stdio.h", "stdlib.h", "string.h", "time.h"},
    "stdint.h": {"inttypes.h"},
}

SKIP_DIRS = {".git", "build", "build-fw", "build-host", "__pycache__", "target",
             ".venv", "node_modules"}


def sources():
    """Every first-party .c and .h. See the C ONLY paragraph above for why the
    C++ files are not here."""
    for p in sorted(ROOT.rglob("*.[ch]")):
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        yield p


def header_index():
    """Basename to path, for the quoted includes CMake resolves by -I."""
    index = {}
    for p in ROOT.rglob("*.h"):
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        index.setdefault(p.name, []).append(p)
    return index


HEADERS = header_index()


def resolve_local(path, rel):
    """Next to the file first, then by unique basename. None if ambiguous."""
    direct = (path.parent / rel).resolve()
    if direct.exists():
        return direct
    hits = HEADERS.get(Path(rel).name, [])
    return hits[0].resolve() if len(hits) == 1 else None


def blank(match):
    """Replace a comment or literal with spaces, KEEPING its newlines.

    Collapsing a multi-line comment to one space shifts every line number after
    it, which is how the first run of this tool reported the defect in
    freqcount.h at line 23 when it was at line 357. A guard that names the wrong
    line sends a reader to the wrong place, which is worse than naming none.
    """
    return "".join("\n" if c == "\n" else " " for c in match.group(0))


def stripped(text):
    return STRING.sub(blank, COMMENT.sub(blank, text))


def direct_includes(text):
    return set(INCLUDE_SYS.findall(text))


def satisfies(have, header):
    return header in have or bool(have & ALSO_PROVIDES.get(header, set()))


def local_closure(path, seen=None):
    """System headers reachable through this file's first-party includes."""
    if seen is None:
        seen = set()
    if path in seen or not path.exists():
        return set()
    seen.add(path)
    text = path.read_text(encoding="utf-8", errors="replace")
    have = direct_includes(text)
    for rel in INCLUDE_LOCAL.findall(text):
        target = resolve_local(path, rel)
        if target is not None:
            have |= local_closure(target, seen)
    return have


def main():
    findings = []
    checked = 0

    for path in sources():
        text = path.read_text(encoding="utf-8", errors="replace")
        body = stripped(text)
        checked += 1
        is_header = path.suffix in (".h", ".hpp")
        have = direct_includes(text)
        if not is_header:
            have |= local_closure(path)

        for pattern, header in NEEDS:
            m = pattern.search(body)
            if m and not satisfies(have, header):
                findings.append((path, header, m.group(0),
                                 body.count("\n", 0, m.start()) + 1))

    rel = lambda p: p.relative_to(ROOT).as_posix()

    for path, header, name, line in findings:
        print("  MISSING  %s" % rel(path))
        print("           uses %-12s at about line %d, does not include <%s>"
              % (name, line, header))

    print()
    print("  %d files checked, %d missing include%s"
          % (checked, len(findings), "" if len(findings) == 1 else "s"))
    print()
    if findings:
        print("  A header that uses a name must include the header that")
        print("  provides it. Inheriting it from the including file works")
        print("  until something includes this header first.")
        return 1

    print("  Every file includes what its own text uses, for bool, the")
    print("  sized integers, the stddef names and the varargs macros.")
    print("  That is a text check and not a compile: it catches one class,")
    print("  the one that cost a round trip on Tuesday 6 October 2026.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
