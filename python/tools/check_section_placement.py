#!/usr/bin/env python3
"""Every named section a C file places a variable in must be collected by the linker.

WHY THIS EXISTS. On Friday 16 October 2026, reading P06's transfer-engine back end
before working on it, its sample buffer was found to carry
`__attribute__((section(".dma_buffer")))` while `c/ld/stm32h7a3zi.ld` collects
`.dma_buffers` and `.dma_buffers*`. A pattern ending in `*` matches names that start
with it, so a name one character SHORTER matches neither. No rule in the script
places that buffer.

WHY THAT PARTICULAR ONE MATTERS, and it is the reason this check is worth its lines.
The file's own header calls the placement the most important comment in this
repository: on this part the main transfer engines cannot reach the tightly coupled
memories at all, so a buffer that lands in DTCM produces a transfer that moves
nothing and reports no error. The linker script's own comment says the same and names
the attribute to use. So the one section attribute in the volume whose correctness is
load bearing was the one that did not match, and the defect is a missing letter.

WHAT IT CHECKS. Every `section(".name")` attribute in first-party C and C++ under
`c/` and `projects/*/{c,cpp}`, against every `*(pattern)` input-section rule in the
linker script, with ld's glob rules: `*` any run of characters, `?` one, `[...]` a
class. A name matched by no pattern is reported, with the closest patterns named so
the fix is obvious rather than a search.

WHAT IT READS, AND WHAT IT REFUSES TO READ. Comment bodies are blanked before
either pattern is applied, in the C sources and in the linker script alike. A check
whose needle appears in prose matches its own explanation: this script's script-side
needle is `*(name)`, and the linker script carries the words "FIRST, before
`*(.text)`, because ..." inside a comment, which the raw text reports as a
thirty-first collection rule where the script has thirty. That one is harmless,
because a real `*(.text)` follows four lines later, but the direction is the
dangerous one: a rule existing only in a comment would make a section that nothing
collects look collected. Found on Saturday 17 October 2026 by comparing the
pattern's hits before and after stripping, which is to say not by it failing.

WHAT IT CANNOT DO. It says nothing about WHICH region a matched section lands in, so
a rule that collects `.dma_buffers` into DTCM would pass this check and be exactly
as wrong as the defect it was written for. The region is a property of the output
section's `> REGION` clause, and reading that correctly means parsing the script's
output sections rather than its input patterns; the map file from a real build is the
authority. This check answers a narrower question, which is whether any rule claims
the section at all, and that is the question the defect turned on. It also does not
see sections created by a `KEEP()` wrapper without a `*(...)` inside it, nor
attributes assembled from macros rather than written as literals.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "c" / "ld" / "stm32h7a3zi.ld"

# __attribute__((section(".name"))) and the aligned() variant beside it; only the
# literal form, which is the only form this repository uses.
ATTRIBUTE = re.compile(r'section\s*\(\s*"(\.[^"]+)"\s*\)')

# An ld input-section rule: *(.text .rodata*) may list several patterns in one.
INPUT_RULE = re.compile(r'\*\s*\(([^)]*)\)')

SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".hpp"}


def code_only(text, line_comments=True):
    """The text with every comment body blanked and the line structure kept.

    Newlines inside a removed span are preserved, so every line number this script
    reports is still the line the construct sits on. String and character literals
    are stepped over rather than scanned, because a section attribute's own name is
    a string literal and a `//` inside one is not a comment. `line_comments` is
    False for a linker script, where `//` is not a comment at all.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        pair = text[i:i + 2]
        if pair == "/*":
            end = text.find("*/", i + 2)
            if end == -1:                      # unterminated: keep nothing after it
                out.append("\n" * text.count("\n", i))
                break
            out.append("\n" * text.count("\n", i, end))
            i = end + 2
            continue
        if line_comments and pair == "//":
            end = text.find("\n", i)
            if end == -1:
                break
            i = end
            continue
        ch = text[i]
        if ch in "\"'":
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == ch or text[j] == "\n":
                    break
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def sources():
    """Every first-party C and C++ file: the shared trees and each project's own."""
    roots = [ROOT / "c"]
    projects = ROOT / "projects"
    if projects.is_dir():
        for d in sorted(projects.iterdir()):
            for lang in ("c", "cpp"):
                if (d / lang).is_dir():
                    roots.append(d / lang)
    out = []
    for top in roots:
        for f in sorted(top.rglob("*")):
            if f.is_file() and f.suffix in SOURCE_SUFFIXES:
                out.append(f)
    return out


def attributes(files):
    """Every (section name, file, line) a section attribute asks for."""
    found = []
    for f in files:
        try:
            text = f.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        # Both the search and the line count use the SAME string. Taking an
        # offset from the stripped text and counting newlines in the original
        # reports a line number that is too small by the size of every comment
        # above it. That is how this was introduced and caught here on
        # Saturday 17 October 2026: the report said startup.c:22 for line 84.
        code = code_only(text)
        for m in ATTRIBUTE.finditer(code):
            line = code[: m.start()].count("\n") + 1
            found.append((m.group(1), f.relative_to(ROOT).as_posix(), line))
    return found


def patterns(script_text):
    """Every input-section pattern the script collects, with its line number."""
    out = []
    script_text = code_only(script_text, line_comments=False)
    for m in INPUT_RULE.finditer(script_text):
        line = script_text[: m.start()].count("\n") + 1
        for word in m.group(1).split():
            if word.startswith("."):
                out.append((word, line))
    return out


def glob_to_regex(pattern):
    """ld's input-section glob: * any run, ? one character, [...] a class."""
    out = ["^"]
    i = 0
    while i < len(pattern):
        ch = pattern[i]
        if ch == "*":
            out.append(".*")
        elif ch == "?":
            out.append(".")
        elif ch == "[":
            j = pattern.find("]", i + 1)
            if j == -1:
                out.append(re.escape(ch))
            else:
                out.append("[" + pattern[i + 1: j] + "]")
                i = j
        else:
            out.append(re.escape(ch))
        i += 1
    out.append("$")
    return re.compile("".join(out))


def matches(name, pats):
    """Every (pattern, line) that claims `name`."""
    return [(p, line) for p, line in pats if glob_to_regex(p).match(name)]


def near(name, pats, limit=3):
    """Patterns whose literal part shares a long prefix with `name`, for the message.

    A missing or extra letter is the defect this check was written for, so the most
    useful thing a failure can print is the pattern the author almost certainly meant.
    """
    scored = []
    for p, line in pats:
        literal = p.split("*")[0]
        common = 0
        for a, b in zip(name, literal):
            if a != b:
                break
            common += 1
        if common >= 4:
            scored.append((common, p, line))
    scored.sort(reverse=True)
    return [(p, line) for _c, p, line in scored[:limit]]


def main():
    if not SCRIPT.is_file():
        print("%s is missing, so nothing here can say what the linker collects"
              % SCRIPT.relative_to(ROOT).as_posix())
        return 2

    text = SCRIPT.read_text(encoding="utf-8")
    pats = patterns(text)
    asked = attributes(sources())

    print("linker script: %s, %d input-section patterns"
          % (SCRIPT.relative_to(ROOT).as_posix(), len(pats)))
    print()

    unmatched = []
    for name, where, line in asked:
        hits = matches(name, pats)
        if hits:
            shown = ", ".join("%s at line %d" % (p, ln) for p, ln in hits[:2])
            print("  ok      %s  (%s:%d)  collected by %s" % (name, where, line, shown))
        else:
            print("  FAIL    %s  (%s:%d)  collected by NOTHING" % (name, where, line))
            for p, ln in near(name, pats):
                print("              did you mean %s, line %d of the linker script?"
                      % (p, ln))
            unmatched.append((name, where, line))

    print()
    print("  %d section attribute%s, %d collected by nothing"
          % (len(asked), "" if len(asked) == 1 else "s", len(unmatched)))
    print()
    if unmatched:
        print("  A section no rule collects is an orphan: the linker places it")
        print("  where it likes rather than where the attribute intended. On this")
        print("  part that is not cosmetic, because the main transfer engines")
        print("  cannot reach the tightly coupled memories at all, so a buffer")
        print("  that lands there moves nothing and reports no error.")
        return 1

    print("  Every section attribute is collected by a rule. This does NOT say")
    print("  which region each lands in: a rule collecting a transfer buffer")
    print("  into DTCM would pass here and be just as wrong. The map file from a")
    print("  real build is the authority on the region.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
