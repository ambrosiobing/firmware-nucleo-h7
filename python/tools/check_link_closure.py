#!/usr/bin/env python3
"""Every firmware target's source list must be closed under what it includes.

WHY THIS EXISTS. On Tuesday 6 October 2026 P06's three targets were found unable
to link. They list c/instr/freqcount.c, which includes and calls into
c/instr/lseref.c, and lseref.c was in neither the target's sources nor the board
library. Three undefined symbols, and nothing had ever caught them.

WHY NOTHING CAUGHT THEM, which is the part worth keeping. Those three targets are
gated behind CMSIS_DEVICE_DIR, which is empty unless a CMSIS pack is passed on
the command line. Continuous integration passes no pack, so CI skips them. The
one laptop with a pack had never been asked to build them. A target skipped on
every machine is a target nobody compiles, and CMakeLists.txt already said so in
a comment about P06 having "no independent build check" without drawing this
conclusion from it.

WHAT IT CHECKS. This repository keeps one .c beside each .h, so a source that
includes "lseref.h" needs lseref.c linked. For each add_firmware target the
includes are followed transitively from its listed sources AND from the board
library's, because those objects link into every target and what they reach has
to be linked too. Any first-party .c reached that way but present in neither set
is reported. That is a link check without a linker.

WHAT IT CANNOT DO. It does not know which symbols a source actually calls, so a
header included for a type alone, with no function used, is reported as needed
when the linker would not require it. That direction is the safe one: it over-
reports rather than under-reports, and a .c listed and not needed costs a few
bytes rather than a build. It also does not resolve arbitrary CMake variables; a
`set(VAR some/path.c)` is followed, and anything else is reported as unresolved
rather than skipped in silence.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CMAKE = ROOT / "CMakeLists.txt"

INCLUDE = re.compile(r'#\s*include\s*"([^"]+)"')
C_PATH = re.compile(r"[\w/.\-]+\.c\b")
VAR = re.compile(r"\$\{(\w+)\}")


def board_objects(cmake_text):
    """The .c files in the board library, which every target links."""
    m = re.search(r"add_library\(board OBJECT(.*?)\)", cmake_text, re.S)
    if not m:
        return set()
    return set(C_PATH.findall(m.group(1)))


def source_variables(cmake_text):
    """Every set(NAME value) whose value names a .c file."""
    out = {}
    for name, value in re.findall(r"set\((\w+)\s+([^)]*)\)", cmake_text):
        paths = C_PATH.findall(value)
        if paths:
            out.setdefault(name, set()).update(paths)
    return out


def header_to_source(header, including_file):
    """The .c beside a first-party header, or None."""
    if not header.endswith(".h"):
        return None
    for base in (including_file.parent, ROOT):
        candidate = (base / header).resolve()
        if candidate.exists():
            source = candidate.with_suffix(".c")
            if source.exists():
                try:
                    return source.relative_to(ROOT).as_posix()
                except ValueError:
                    return None
            return None
    return None


def reachable(seeds):
    """Every first-party .c reached transitively through includes."""
    need, seen, queue = set(), set(), list(seeds)
    while queue:
        rel = queue.pop()
        if rel in seen:
            continue
        seen.add(rel)
        path = ROOT / rel
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for header in INCLUDE.findall(text):
            source = header_to_source(header, path)
            if source:
                need.add(source)
                queue.append(source)
    return need


def main():
    cmake = CMAKE.read_text(encoding="utf-8")
    board = board_objects(cmake)
    variables = source_variables(cmake)

    targets = re.findall(
        r"add_firmware\((\S+)\s+SOURCES(.*?)(?:INCLUDES|VENDOR_INCLUDES|DEFINES|\))",
        cmake, re.S)
    if not targets:
        print("  no add_firmware targets found in CMakeLists.txt, which cannot")
        print("  be right and means this check is reading the wrong thing")
        return 1

    findings = []
    unresolved = []
    for name, block in targets:
        listed = set(C_PATH.findall(block))
        for var in VAR.findall(block):
            if var in variables:
                listed |= variables[var]
            elif var.endswith(("NAME", "TARGET")):
                pass          # a target name, not a source
            else:
                unresolved.append((name, var))
        if not listed:
            continue
        # Seeded with the board library's sources as well as the target's own.
        # The board objects link into every target, so what THEY reach has to be
        # linked too, and an earlier version seeded only from the target's list:
        # dropping c/clock/clocktree.c from the board library, which
        # c/board/system.c includes, was not caught, because nothing in any
        # target's own list mentions either file.
        missing = sorted(reachable(listed | board) - listed - board)
        if missing:
            findings.append((name, missing))

    for name, var in unresolved:
        print("  UNRESOLVED %s uses ${%s}, which this check cannot expand, so"
              % (name, var))
        print("             its source list was checked without it")

    for name, missing in findings:
        print("  NOT CLOSED %s" % name)
        for m in missing:
            print("             reaches %s through its includes and does not"
                  " link it" % m)

    print()
    print("  %d firmware targets, %d not closed, %d unresolved variable%s"
          % (len(targets), len(findings), len(unresolved),
             "" if len(unresolved) == 1 else "s"))
    print()
    if findings or unresolved:
        print("  A target that reaches a first-party .c through its includes")
        print("  must link that .c. This repository keeps one .c beside each")
        print("  .h, so the include is the dependency. A target gated behind")
        print("  an optional pack is skipped on every machine that lacks it,")
        print("  which is how three undefined symbols sat in P06 unnoticed.")
        return 1

    print("  Every firmware target links every first-party source it reaches.")
    print("  That is a link check without a linker: it follows includes, not")
    print("  symbols, so it over-reports rather than under-reports.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
