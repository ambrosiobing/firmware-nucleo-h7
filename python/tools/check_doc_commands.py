#!/usr/bin/env python3
"""Every path a documented command names must exist.

WHY THIS EXISTS. `check_links.py` validates every Markdown link in the volume and
nothing validated the commands, so a reader could be handed a path that a rename had
moved. On Sunday 18 October 2026 a sweep of the leading path arguments of every
`python` and `pytest` invocation found 36 documented paths and 3 that did not exist:
P09's README offered `python -m pytest tests -q` where the suite is `python/tests`,
and P12's README named `python/gates.py` twice for a program at
`projects/P12-energy-regression/python/gates.py`. A fourth, in chapter 9's build
recipe, was already in hand from a grep.

WHY IT IS WORTH A FILE WHERE AN EARLIER CANDIDATE WAS NOT. The same day, a checker
was declined for comments claiming something is "in this file": that class had a
population of two sites, of which one was wrong, and a check would have had to tell a
quoted claim from a live one. This class has 36 paths across 136 documents and had
three wrong on its first run, and the question it asks, does this path exist, has an
answer that needs no judgement.

HOW IT FAILS LOUDLY RATHER THAN QUIETLY. The command that prompted this exits 4 with
"no tests ran", which is visible. A path that has moved into a directory that still
exists is the quiet case, and the figure beside it is worse than the command: P09's
README claimed twenty-one checks from an invocation that could produce none.

WHAT IT CHECKS. Markdown under the repository, excluding `build` directories, for
these invocation shapes, taking the arguments that are paths rather than flags:

    python -m pytest <path> [<path> ...]
    python <path>.py
    python3 the same two

A token is skipped when it begins with a dash, holds a shell or template character,
or names no path-like candidate. Every surviving token is checked against the working
tree.

WHAT IT CANNOT DO, and each of these is a real hole rather than a formality.

It reads only the leading path arguments, so a path inside a flag's value, after a
shell variable, or spanning a line continuation is invisible to it. Chapter 9's
recipe spans three lines with a trailing backslash and only its first token is seen.

It says nothing about whether a command that runs does the right thing. P12's
README named a real program with the wrong path; had the path been right and the
sub-command wrong, this would pass.

It cannot see a path that exists but is wrong, which is the quiet failure. A rename
from `python/tests` to `python/tests2` with both present passes here.

And it does not check commands in the published chapters, which are written by
`mdbuild.py` from sources this repository does not track, so a defect there has to be
fixed in a `.tex` file that never appears in `git status`. Chapter 9's recipe was
fixed that way on Sunday 18 October 2026.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# The two invocation shapes. Group 1 is the run of candidate arguments.
PATTERNS = (
    re.compile(r"python(?:3)?\s+-m\s+pytest\s+((?:[\w./\\-]+\s*)+)"),
    re.compile(r"python(?:3)?\s+((?:python|projects|c|cpp|rust|docs)[\w./\\-]*\.py)"),
)

# A token that cannot be a path in this repository: a flag, a shell or template
# construct, or a Windows drive letter, which appears in the `cd` half of every
# hand-over command and is not this repository's to check.
SKIP = re.compile(r"^-|[%*<>$\"']|^[A-Za-z]:")


def documents():
    """Every Markdown file except build output."""
    out = []
    for md in sorted(ROOT.rglob("*.md")):
        parts = set(md.parts)
        if ".git" in parts or "build" in parts or "build-host" in parts:
            continue
        out.append(md)
    return out


def claims(md):
    """Every (path, line) a documented command in this file names."""
    text = md.read_bytes().decode("utf-8", "replace").replace("\r\n", "\n")
    found = []
    for pat in PATTERNS:
        for m in pat.finditer(text):
            line = text[: m.start()].count("\n") + 1
            for token in m.group(1).split():
                if SKIP.search(token):
                    continue
                found.append((token.replace("\\", "/"), line))
    return found


def main():
    missing = []
    checked = 0
    for md in documents():
        rel = md.relative_to(ROOT).as_posix()
        for token, line in claims(md):
            checked += 1
            if not (ROOT / token).exists():
                missing.append((rel, line, token))

    for rel, line, token in missing:
        print("  MISSING  %s:%d  names %s" % (rel, line, token))

    print("\n  %d documented paths checked, %d that do not exist" % (checked, len(missing)))
    if missing:
        print("\n  A documented command that cannot run is a defect even when it")
        print("  fails loudly. Fix the path, and check the figure beside it: the")
        print("  one that prompted this check claimed twenty-one checks from an")
        print("  invocation that could produce none.")
        return 1

    print("\n  Every path a documented command names exists. That does NOT mean")
    print("  the commands are right: this reads leading path arguments only, so a")
    print("  path in a flag's value or after a line continuation is invisible, and")
    print("  a path that exists but is the wrong one passes. See this file's")
    print("  header for the full list of what it cannot do.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
