#!/usr/bin/env python3
"""Every interrupt handler this repository defines must have a vector to reach it.

WHY THIS EXISTS. On Wednesday 7 October 2026 P06's converter bring-up completed with
all eight steps reporting ok, every register reading back what was written, the
converter calibrated, enabled, armed and triggered, and `acq_start` returning 0. No
sample ever arrived and the console stopped mid-word. `ADC_IRQHandler` existed and was
declared weak, so it compiled and linked; it was simply not in the vector table, so
the first end-of-conversion read a vector from past the end of that array and branched
into `.text`. The lesson is recorded at length in `c/board/startup.c` and in
`c/board/stm32h7a3_regs.h`: THREE THINGS MUST BE TRUE FOR A HANDLER TO RUN, the
peripheral's own enable in its IER, the interrupt controller's enable through NVIC,
and a vector for the handler to reach, and only the first two read back. The third has
nothing to read back from, which is why it is the one that goes missing.

AND IT WENT MISSING AGAIN. On Friday 16 October 2026, renaming P06's transfer-engine
handler from stream 0 to stream 1 prompted a one-line look at the table, which had no
DMA entry of any kind. That handler had been in the tree since Saturday 3 October
2026 and could never have run, whatever its engine configuration said. Twice is a
class, and a class is worth a check.

WHAT IT CHECKS. Every function DEFINITION named `*_IRQHandler` in first-party C under
`c/` and `projects/*/c`, against the designated indices in the vector table in
`c/board/startup.c`. A handler named `X_IRQHandler` needs an entry
`[IRQ_VECTOR_INDEX(IRQ_X)]` with X upper-cased, which is the convention the table
already uses: `ADC_IRQHandler` with `IRQ_ADC`, `USART3_IRQHandler` with `IRQ_USART3`,
`DMA1_Stream1_IRQHandler` with `IRQ_DMA1_STREAM1`. It also reports a table entry whose
`IRQ_` name has no `#define` in the register header, because a designated index that
does not compile is a different failure with the same cause.

WHAT IT CANNOT DO. It does not check that the POSITION is right, only that an entry
exists: a vector sitting at the wrong index routes the interrupt to the wrong handler
and passes this check. The positions are cited one by one in `stm32h7a3_regs.h` from
ST's `IRQn_Type` for this part, and the generated table with one test per row that the
improvement plan describes is what would check them. It also sees only the literal
`void X_IRQHandler(void)` definition form, and says nothing about whether the
peripheral's IER bit or the NVIC enable are set, which are the other two of the three
and do read back.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STARTUP = ROOT / "c" / "board" / "startup.c"
REGS = ROOT / "c" / "board" / "stm32h7a3_regs.h"

# A definition rather than a declaration: the brace, not a semicolon. The weak
# aliases in startup.c end in a semicolon and are declarations, so they are not
# definitions and are correctly ignored.
#
# THE CHARACTER CLASSES ADMIT A SPACE AND A TAB ONLY, NEVER \s, AND THE FIRST
# VERSION OF THIS FILE GOT THAT WRONG, which is worth keeping because the symptom was
# the one this tool exists to prevent: \s matches a newline, so with a blank line
# above a definition the match began on the blank line, the reported line number came
# out one low, the brace test below looked at the handler's own line, found no brace,
# and DROPPED THE HANDLER IN SILENCE. ADC_IRQHandler has a blank line above it and
# USART3_IRQHandler has a comment, so the tool reported two handlers where the
# repository defines three and said nothing. Caught by comparing its output against
# a grep by hand, which is the only reason it was caught: a checker that
# under-reports looks exactly like a clean repository.
DEFINITION = re.compile(
    r"^[ \t]*void[ \t]+(\w+_IRQHandler)[ \t]*\([ \t]*void[ \t]*\)[ \t]*$", re.M)
BRACE_NEXT = re.compile(r"^\s*\{")

# [IRQ_VECTOR_INDEX(IRQ_USART3)] = USART3_IRQHandler,
ENTRY = re.compile(r"\[\s*IRQ_VECTOR_INDEX\s*\(\s*(IRQ_\w+)\s*\)\s*\]\s*=\s*(\w+)")
DEFINE = re.compile(r"^\s*#\s*define\s+(IRQ_\w+)\s", re.M)


def sources():
    """Every first-party C file that could define a handler."""
    roots = [ROOT / "c"]
    projects = ROOT / "projects"
    if projects.is_dir():
        for d in sorted(projects.iterdir()):
            if (d / "c").is_dir():
                roots.append(d / "c")
    out = []
    for top in roots:
        for f in sorted(top.rglob("*.c")):
            out.append(f)
    return out


def shown(path):
    """The path as the report should print it: relative to the repository when it
    lies inside it, absolute when it does not. relative_to RAISES on a path outside
    its argument, which made this function unusable on a temporary tree and so
    untestable away from the repository itself; a checker that cannot be pointed at
    a synthetic case is a checker whose own behaviour is taken on trust."""
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def definitions(files):
    """Every (handler name, file, line) actually defined, not merely declared."""
    found = []
    for f in files:
        try:
            text = f.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        lines = text.splitlines()
        for m in DEFINITION.finditer(text):
            line = text[: m.start()].count("\n") + 1
            # the next non-blank line must open a body
            nxt = ""
            for cand in lines[line:]:
                if cand.strip():
                    nxt = cand
                    break
            if BRACE_NEXT.match(nxt):
                found.append((m.group(1), shown(f), line))
    return found


def wanted_macro(handler):
    """IRQ_ name a handler needs, by the convention the table already uses."""
    return "IRQ_" + handler[: -len("_IRQHandler")].upper()


def main():
    for f in (STARTUP, REGS):
        if not f.is_file():
            print("%s is missing, so nothing here can say what the table holds"
                  % f.relative_to(ROOT).as_posix())
            return 2

    table = STARTUP.read_text(encoding="utf-8")
    entries = {m.group(1): m.group(2) for m in ENTRY.finditer(table)}
    defined_macros = {m.group(1) for m in DEFINE.finditer(REGS.read_text(encoding="utf-8"))}
    handlers = definitions(sources())

    print("vector table: %s, %d designated entries"
          % (STARTUP.relative_to(ROOT).as_posix(), len(entries)))
    print()

    unreachable = []
    for name, where, line in sorted(handlers):
        macro = wanted_macro(name)
        if macro in entries and entries[macro] == name:
            print("  ok      %s  (%s:%d)  reached through %s" % (name, where, line, macro))
        elif macro in entries:
            print("  FAIL    %s  (%s:%d)  %s points at %s instead"
                  % (name, where, line, macro, entries[macro]))
            unreachable.append((name, where, line, "the entry names another handler"))
        else:
            print("  FAIL    %s  (%s:%d)  NO VECTOR: the table has no %s"
                  % (name, where, line, macro))
            unreachable.append((name, where, line, "no entry"))

    unbound = sorted(m for m in entries if m not in defined_macros)
    for m in unbound:
        print("  FAIL    %s is used as a designated index and is defined nowhere in %s"
              % (m, REGS.relative_to(ROOT).as_posix()))

    print()
    print("  %d handler%s defined, %d unreachable, %d index name%s unbound"
          % (len(handlers), "" if len(handlers) == 1 else "s", len(unreachable),
             len(unbound), "" if len(unbound) == 1 else "s"))
    print()
    if unreachable or unbound:
        print("  A handler with no vector compiles, links and never runs, and the")
        print("  interrupt it was written for reads a vector from past the end of")
        print("  the table. That cost an evening on Wednesday 7 October 2026, and")
        print("  the same state was found again on Friday 16 October 2026.")
        print("  Source the position from ST's IRQn_Type for this part, cite it in")
        print("  stm32h7a3_regs.h, and add the entry.")
        return 1

    print("  Every defined handler has an entry naming it. This does NOT check the")
    print("  POSITION: an entry at the wrong index routes to the wrong handler and")
    print("  passes here. The positions are cited one by one in the register")
    print("  header, and a generated table with one test per row is what would")
    print("  check them.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
