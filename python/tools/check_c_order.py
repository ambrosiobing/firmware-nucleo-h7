"""Every static function in this repository's C is defined before it is called.

**Why this exists, and it is a defect rather than a precaution.** C needs a
declaration before a call. A call to a function defined later in the same file,
with no prototype above it, is an implicit declaration: the compiler assumes it
returns `int` and takes anything, and then objects when the real definition
disagrees. Under this repository's warning set that is three errors and two
warnings from one misplaced function.

It happened on Sunday 4 October 2026. A voltage scaling helper was inserted into
`c/board/clock280.c` above the two functions it calls, and the first thing to
notice was gcc 15 in WSL on bing@JPTOUPM678, then arm-none-eabi-gcc 14.3.1 on
win11 skyhorizon a round trip later. Nothing on win11 aquamarine could have seen
it, because compiling there is forbidden and so the C is written on a laptop that
never compiles it.

So this is the C counterpart of `check_names.py`: the one class of defect that is
visible without running a compiler, checked on the laptop that cannot run one.
`check_c_syntax.py` beside it is the real check and refuses on Windows by design;
this one deliberately does not compile anything and therefore runs everywhere.

**What it does not do.** It is not a C parser. It finds `static` definitions and
`static` prototypes by pattern, and reports a call that appears earlier in the
file than the definition with no prototype above it. A forward declaration is
accepted, which is the correct answer: declaring before calling is exactly what
C asks for, and plenty of files legitimately do that for mutual recursion.

Non-static functions are not checked. They are declared in a header the file
includes, and whether that header was included is a question for the compiler.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# A static definition: `static <type> name(` at the start of a line. The type may
# carry `const` and any number of stars.
DEFINITION = re.compile(
    r"^static\s+(?:const\s+)?[A-Za-z_][\w \t*]*?\b(\w+)\s*\(", re.M)

# A static prototype: the same shape, ending in a semicolon rather than a brace.
PROTOTYPE = re.compile(r"^static\s+[^;{]*?\b(\w+)\s*\([^;{]*\)\s*;", re.M)


def without_comments(text: str) -> str:
    """Comments blanked but the length preserved, so offsets still locate lines.

    Replacing a comment with nothing would shift every offset after it and the
    reported line numbers would be wrong, which for a tool whose whole output is
    a line number would be worse than no tool.
    """
    text = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)


def calls_before_definition(path: Path) -> list[tuple[str, int, int]]:
    """Each (name, call line, definition line) where the call comes first."""
    code = without_comments(path.read_text(encoding="utf-8"))
    declared = {m.group(1) for m in PROTOTYPE.finditer(code)}

    defined: dict[str, int] = {}
    for m in DEFINITION.finditer(code):
        defined.setdefault(m.group(1), m.start())

    found = []
    for name, at in defined.items():
        if name in declared:
            continue
        for call in re.finditer(r"\b" + re.escape(name) + r"\s*\(", code):
            if call.start() < at:
                found.append((name,
                              code[:call.start()].count("\n") + 1,
                              code[:at].count("\n") + 1))
                break
    return found


def files():
    for folder in ("c", "projects"):
        for path in sorted((ROOT / folder).rglob("*.c")):
            yield path


def main() -> int:
    checked = 0
    problems = 0
    for path in files():
        checked += 1
        for name, call_line, def_line in calls_before_definition(path):
            problems += 1
            print("{}:{}: {} is called here and defined at line {}. Move the "
                  "definition above the call, or declare it first."
                  .format(path.relative_to(ROOT).as_posix(), call_line, name,
                          def_line))
    print("{} C files, {}".format(
        checked,
        "every static function is defined before it is called" if not problems
        else "{} calls before a definition".format(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
