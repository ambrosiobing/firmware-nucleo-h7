#!/usr/bin/env python3
"""Every printf-family call's conversion count against its argument count.

WHY THIS EXISTS. The worst defect this project has produced was six arguments
passed into a five-placeholder format string. Python's str.format discarded the
extra in silence, three implementations then agreed with each other while all
three were wrong, and only the two that format in their own languages caught it.
The C side of that class is caught by gcc's -Wformat, but only when a compiler
runs, and compiling is forbidden on the authoring laptop. This runs anywhere.

It has already earned its place while living in a scratch directory: it was run
by hand against projects/P01-toolchain-first-light/c/pll280.c in five separate
commits on Tuesday 6 October 2026, across 85, 87, 94, 95 and 109 calls, while
that file's report text was being rewritten repeatedly. A mismatch there prints a
wrong number to the console, where it reads as a measurement.

WHAT IT CHECKS, and the per-function format position, because they differ:

    printf(fmt, ...)                  format is argument 0
    fprintf(stream, fmt, ...)         format is argument 1
    sprintf(buf, fmt, ...)            format is argument 1
    snprintf(buf, size, fmt, ...)     format is argument 2

The v-prefixed forms take a va_list rather than arguments, so there is nothing to
count and they are skipped by name rather than silently mishandled.

HOW THE PARSE WORKS, and the mistake it was written around. The call's argument
list is split on TOP LEVEL commas with string literals and character literals
treated as atoms, so the format is one field and each argument is one field. An
earlier version took everything after the last quote in the call, which put zero
arguments on every call containing a ternary such as `ok ? "yes" : "no"` and
reported six false mismatches in one file. A checker that cries wolf gets
switched off, so the parse is the part that had to be right.

WHAT IT CANNOT DO. It reads text, so a format string held in a variable or built
by the preprocessor is skipped rather than guessed at, and it does not check that
%lu was given an unsigned long rather than an int. It counts. gcc's -Wformat
checks the types, on the laptop that has a compiler, and the two are complements
rather than substitutes.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

STRING = re.compile(r'"(?:\\.|[^"\\])*"')
CHARLIT = re.compile(r"'(?:\\.|[^'\\])*'")
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
# One optional space is a flag in C; a RUN of spaces is not, and the earlier
# pattern allowed runs, so "%  d" matched across ordinary prose. Flags, then
# width, then precision, then length, then the conversion.
SPEC = re.compile(
    r"%[-+ #0]*[0-9]*(?:\.(?:[0-9]+|\*))?(?:hh|h|ll|l|j|z|t|L)?"
    r"[diouxXeEfFgGaAcspn%]")

# The <inttypes.h> macros, which this repository uses correctly and which an
# earlier version of this tool dropped on the floor. Each contributes exactly one
# conversion letter to the format when a % precedes it, which is all a COUNT
# needs; the real expansion differs by platform and does not matter here.
PRI = re.compile(r"\bPRI([diouxX])(?:8|16|32|64|MAX|PTR|LEAST(?:8|16|32|64)"
                 r"|FAST(?:8|16|32|64))\b")

# function name -> which argument holds the format string
FORMAT_AT = {
    "printf": 0,
    "fprintf": 1,
    "sprintf": 1,
    "snprintf": 2,
}
SKIP_NAMES = ("vprintf", "vfprintf", "vsprintf", "vsnprintf")

CALL = re.compile(r"\b(" + "|".join(FORMAT_AT) + r")\s*\(")

SKIP_DIRS = {".git", "build", "build-fw", "build-host", "__pycache__", "target",
             ".venv", "node_modules"}


def blank(match):
    """Replace a comment with spaces, keeping its newlines so lines still count."""
    return "".join("\n" if c == "\n" else " " for c in match.group(0))


def sources():
    for pattern in ("*.c", "*.cpp"):
        for p in sorted(ROOT.rglob(pattern)):
            if any(part in SKIP_DIRS for part in p.parts):
                continue
            yield p


def split_args(src, after_paren):
    """Fields of one call's argument list, and the index just past its ')'."""
    i, depth, fields, cur = after_paren, 1, [], ""
    while i < len(src) and depth:
        ch = src[i]
        if ch == '"':
            m = STRING.match(src, i)
            if m:
                cur += m.group(0)
                i = m.end()
                continue
        if ch == "'":
            m = CHARLIT.match(src, i)
            if m:
                cur += m.group(0)
                i = m.end()
                continue
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                i += 1
                break
        if ch == "," and depth == 1:
            fields.append(cur)
            cur = ""
        else:
            cur += ch
        i += 1
    if cur.strip():
        fields.append(cur)
    return fields, i


def conversions(field):
    """The conversions in a format field, or None when it is not countable.

    None means SKIPPED, and the distinction matters: a field holding a variable,
    an unknown macro or anything else this cannot read is declined rather than
    counted with the unreadable part dropped. Dropping a token and counting
    anyway produced six confident wrong answers the first time this ran.
    """
    # PRI macros first, so that "%" PRIu64 becomes "%" "u" and joins correctly
    field = PRI.sub(r'"\1"', field)

    literals = STRING.findall(field)
    if not literals:
        return None

    # whatever is left once the literals are removed must be only separators
    rest = STRING.sub("", field).strip()
    if rest:
        return None

    fmt = "".join(s[1:-1] for s in literals)
    return [s for s in SPEC.findall(fmt) if s != "%%"]


def main():
    findings = []
    checked = 0
    skipped_dynamic = 0

    for path in sources():
        src = COMMENT.sub(blank, path.read_text(encoding="utf-8", errors="replace"))

        for m in CALL.finditer(src):
            name = m.group(1)
            if name in SKIP_NAMES:
                continue
            fields, _ = split_args(src, m.end())
            at = FORMAT_AT[name]
            if len(fields) <= at:
                continue
            specs = conversions(fields[at])
            if specs is None:
                skipped_dynamic += 1
                continue
            checked += 1
            args = [f for f in fields[at + 1:] if f.strip()]
            if any("*" in s for s in specs):
                # a %*d takes its width from an argument; not modelled, so the
                # call is reported as unchecked rather than counted wrongly
                skipped_dynamic += 1
                checked -= 1
                continue
            if len(specs) != len(args):
                findings.append((path, src.count("\n", 0, m.start()) + 1, name,
                                 specs, [a.strip()[:40] for a in args]))

    for path, line, name, specs, args in findings:
        print("  MISMATCH %s:%d" % (path.relative_to(ROOT).as_posix(), line))
        print("           %s has %d conversions and %d arguments"
              % (name, len(specs), len(args)))
        print("           conversions: %s" % specs)
        print("           arguments:   %s" % args)

    print()
    print("  %d calls checked, %d mismatch%s, %d skipped as not countable"
          % (checked, len(findings), "" if len(findings) == 1 else "es",
             skipped_dynamic))
    print()
    if findings:
        print("  A format string and its arguments must agree in number. One")
        print("  extra argument is discarded in silence and one too few reads")
        print("  whatever is next on the stack, and both print a number that")
        print("  looks like a measurement.")
        return 1

    print("  Every countable printf-family call agrees with its arguments.")
    print("  That is a count and not a type check: gcc's -Wformat checks the")
    print("  types on the laptop that has a compiler, and this runs anywhere.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
