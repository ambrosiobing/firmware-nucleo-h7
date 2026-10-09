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


def foreach_values(cmake_text):
    """Every foreach(NAME a b c) and the literal words it iterates over.

    Needed because a set() inside a loop names its file through the loop
    variable, so the path is not a literal and cannot be resolved without
    knowing what the loop supplies.
    """
    out = {}
    for name, items in re.findall(r"foreach\((\w+)\s+([^)]*)\)", cmake_text):
        words = [w for w in items.split() if re.fullmatch(r"[\w.\-]+", w)]
        if words:
            out.setdefault(name, []).extend(words)
    return out


def source_variables(cmake_text, loops):
    """Every set(NAME value) whose value names a .c file.

    TWO THINGS THIS GOT WRONG UNTIL Wednesday 7 October 2026, and both were
    silent, which is the only kind worth a comment this long.

    The value pattern was [^)]*, which TERMINATES AT THE FIRST ")". A value of
    projects/P06-timer-sampling/c/acq_${BACKEND}.c contains one inside ${...},
    so the capture stopped at "acq_${BACKEND", no .c path was found in it, and
    that set() contributed nothing at all. The pattern now allows ${NAME}
    through so the whole value is captured.

    And a captured ${NAME} still is not a filename. Rather than drop it, the
    loop values are substituted, so acq_${BACKEND}.c with
    foreach(BACKEND systick timer dma) yields all three. Anything still
    unresolved is returned as such and reported by the caller rather than
    skipped, because a source nobody can name is exactly the one that goes
    unlinked.

    WHAT THE OLD BEHAVIOUR COST. P06 sets ACQ three times, once per back end,
    and only the one with no ${} in it parsed. So acq_dma_double.c was the only
    back end this check ever followed, and the includes of acq_timer.c and
    acq_systick.c were never read. The check reported P06 closed on the strength
    of one third of it, and said nothing about the other two for the whole of
    its existence.
    """
    out = {}
    unresolved = {}
    pattern = re.compile(r"set\((\w+)\s+((?:[^()]|\$\{\w+\})*)\)")
    for name, value in pattern.findall(cmake_text):
        for candidate in _expand(value, loops):
            paths = C_PATH.findall(candidate)
            if paths:
                out.setdefault(name, set()).update(paths)
            elif "${" in candidate and ".c" in candidate:
                unresolved.setdefault(name, set()).add(candidate.strip())
    return out, unresolved


def _expand(value, loops):
    """One value with every ${NAME} replaced by each of its loop words."""
    names = [v for v in VAR.findall(value) if v in loops]
    if not names:
        return [value]
    out = [value]
    for name in names:
        nxt = []
        for text in out:
            for word in loops[name]:
                nxt.append(text.replace("${%s}" % name, word))
        out = nxt
    return out


def header_to_source(header, including_file, include_dirs=()):
    """The .c beside a first-party header, or None.

    THE SEARCH PATH IS THE COMPILER'S, which it was not until Wednesday
    7 October 2026. It used to look only beside the including file and at the
    repository root, so a quoted include of a header in ANOTHER first-party
    directory resolved to nothing and was silently not followed.

    That is why include_dirs is threaded in from each target's own INCLUDES
    list. P06's targets pass "projects/P06-timer-sampling/c c/instr", which is
    how projects/P06-timer-sampling/c/acq_timer.c reaches c/instr/adcmath.h, and
    before this it did not: every c/instr include from a project file was
    invisible. The one case that did work, and the one this check was written
    for, was c/instr/freqcount.c reaching c/instr/lseref.h, because those two
    sit in the same directory.
    """
    if not header.endswith(".h"):
        return None
    bases = [including_file.parent]
    bases.extend(ROOT / d for d in include_dirs)
    bases.append(ROOT)
    for base in bases:
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


def reachable(seeds, include_dirs=()):
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
            source = header_to_source(header, path, include_dirs)
            if source:
                need.add(source)
                queue.append(source)
    return need


def strip_cmake_comments(text):
    """Remove # comments, because a ')' inside one truncates every parse below.

    THE THIRD BLIND SPOT IN THIS CHECK, found Friday 9 October 2026, and the most
    embarrassing of the three. Every pattern here that spans a multi-line list
    ends at the first INCLUDES, VENDOR_INCLUDES, DEFINES or ')'. A CMake comment
    can contain any of those, and P06's SOURCES list carries a long comment
    holding the words "a set()". The parse stopped at that parenthesis, so
    c/instr/adcmath.c, c/instr/pwmmath.c and ${ACQ} itself were all invisible,
    which means THIS CHECK HAD NOT FOLLOWED ANY OF P06'S THREE BACK ENDS SINCE
    Wednesday 7 October 2026.

    The comment that broke it is the one describing the first two defects this
    check was fixed for. A note about two blind spots created a third.

    It surfaced only because a new source file was added ABOVE the truncation
    point, so its includes were followed while the .c files satisfying them sat
    below it and read as missing. Had the file been added below, nothing would
    have been reported at all.
    """
    return re.sub(r"#[^\n]*", "", text)


def main():
    cmake = strip_cmake_comments(CMAKE.read_text(encoding="utf-8"))
    board = board_objects(cmake)
    loops = foreach_values(cmake)
    variables, unresolved_sets = source_variables(cmake, loops)

    targets = re.findall(
        r"add_firmware\((\S+)\s+SOURCES(.*?)(?:INCLUDES|VENDOR_INCLUDES|DEFINES|\))",
        cmake, re.S)

    # Each target's own INCLUDES list, which is the search path its compiler
    # gets and therefore the search path this check has to use. Parsed by
    # splitting on the call rather than with one regex over both lists, because
    # a target name can hold ${...} and the lists are multi-line.
    include_dirs = {}
    for chunk in cmake.split("add_firmware(")[1:]:
        words = chunk.split()
        if not words:
            continue
        found = re.search(r"INCLUDES(.*?)(?:VENDOR_INCLUDES|DEFINES|\)|$)",
                          chunk, re.S)
        include_dirs[words[0]] = (
            [w for w in found.group(1).split() if "/" in w] if found else [])
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
        dirs = include_dirs.get(name, [])
        missing = sorted(reachable(listed | board, dirs) - listed - board)
        if missing:
            findings.append((name, missing))

    for name, var in unresolved:
        print("  UNRESOLVED %s uses ${%s}, which this check cannot expand, so"
              % (name, var))
        print("             its source list was checked without it")

    for name, values in sorted(unresolved_sets.items()):
        for value in sorted(values):
            print("  UNRESOLVED set(%s ...) names %s, which still holds a"
                  % (name, value))
            print("             ${...} this check cannot expand, so that source"
                  " was never followed")

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
    if findings or unresolved or unresolved_sets:
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
