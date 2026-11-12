"""A project may not name a part its own chapter rules off that shield.

WHY THIS EXISTS. On Monday 9 November 2026 two authored files, the P14 roll
call's expected table and P14's README, named the LSM6DSO16IS as the part
answering 0x6B on the X-NUCLEO-IKS5A1, and built a finding on top of it: that
the two shields strap ONE shared part to swapped addresses. The count is two
and not three: the projects front page escaped only because its P14 row did not
mention the part at all, which is a stale page rather than a correct one. Chapter 14 had said the opposite since it was written. Its step 1
states that finding an LSM6DSO16IS means holding a different board, and its
complement sentence lists four corroborating sources for the five parts the
card does carry. The name had come from a Raspberry Pi driver table that maps
the identification value 0x22 to that catalogue number, and the industrial
sibling on this shield answers the same value.

Every gate was green for three days. check_status.py checks that paths exist,
check_links.py that links resolve, and the suite had 1028 tests, none of which
could see that a project and its chapter disagreed about which parts are on a
board. The claim was also the kind that reads as a result, which is the worst
kind to leave unchecked.

So this closes that direction: for each shield whose chapter names parts as NOT
on the card, no file under that shield's project may name one. It is deliberately
asymmetric and does not try to check that the right parts ARE named, because a
roll call that honestly prints an unnamed value is correct and a test demanding
a name would push back towards guessing one.

TWO THINGS IT MUST NOT BREAK, both found by running it before believing it. A
correction has to name what it corrects, so a mention sitting next to a
withdrawal is the fixed state and not the defect. And a capture under console/
is a raw record of what an image printed, which is never edited afterwards: the
refusal of Monday 9 November 2026 carries the wrong name because the image
carried it, and rewriting that file would be falsifying evidence. So captures
are skipped and every other mention must carry a withdrawal marker within
MARKER_WINDOW characters.

MEASURED AGAINST THE DEFECT, which is the only thing that makes it a gate
rather than a sentence. Run against `git show HEAD` of the two files as they
stood before the correction of Thursday 12 November 2026, it fires on
`c/rollcall.c` line 69 and `README.md` line 138, and the capture under
`console/` is skipped in both states. A guard that passes on the fixed tree and
was never shown the broken one proves only that it compiles.
"""
from __future__ import annotations

import re
from pathlib import Path

import pytest

from conftest import ROOT

# The excluded part is read from the chapter rather than written here, so the
# chapter stays the single statement of it. The sentence is chapter 14's step 1.
EXCLUSIONS = {
    "P14-iks5a1-dual-range": (
        "chapters/14-the-iks5a1.md",
        ("LSM6DSV32X", "LSM6DSO16IS"),
    ),
}

READ_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".py", ".rs", ".md"}

# A mention is allowed when it is being withdrawn or denied. These are the words
# that do that in this repository's prose, and the window is characters on
# either side of the part name rather than lines, because the name appears both
# in running prose and in a one line table entry.
MARKERS = (
    "withdraw", "different board", "does not carry", "not on this",
    "rules that part off", "not evidence", "keep it off", "off this shield",
    "the other card", "the iks4a1's part",
)   # matched case-insensitively: this repository writes its headings in capitals
MARKER_WINDOW = 320


@pytest.mark.parametrize("project", sorted(EXCLUSIONS))
def test_the_chapter_still_excludes_those_parts(project: str) -> None:
    """The control: if the chapter stops excluding a part, this test is stale."""
    chapter_rel, parts = EXCLUSIONS[project]
    chapter = (ROOT / chapter_rel).read_text(encoding="utf-8")
    for part in parts:
        assert part in chapter, (
            f"{chapter_rel} no longer mentions {part}. Either the chapter "
            f"changed its complement or the part name changed; this test's "
            f"exclusion list is stale and must be re-read from the chapter.")
    assert "you are holding a different board" in chapter, (
        f"{chapter_rel} no longer carries step 1's exclusion sentence, which "
        f"is what this test enforces for the project directory.")


@pytest.mark.parametrize("project", sorted(EXCLUSIONS))
def test_no_project_file_names_an_excluded_part(project: str) -> None:
    chapter_rel, parts = EXCLUSIONS[project]
    offenders: list[str] = []
    for path in sorted((ROOT / "projects" / project).rglob("*")):
        if not path.is_file() or path.suffix not in READ_SUFFIXES:
            continue
        if "console" in path.relative_to(ROOT).parts:
            continue   # a capture is a raw record and is never edited
        text = path.read_text(encoding="utf-8", errors="replace")
        for part in parts:
            for m in re.finditer(re.escape(part), text):
                lo = max(0, m.start() - MARKER_WINDOW)
                window = text[lo:m.end() + MARKER_WINDOW]
                lowered = window.lower()
                if any(k in lowered for k in MARKERS):
                    continue
                rel = path.relative_to(ROOT).as_posix()
                line = text.count(chr(10), 0, m.start()) + 1
                offenders.append(f"{rel}:{line} names {part} as a fact")
    assert not offenders, (
        f"{chapter_rel} says a board carrying these parts is a DIFFERENT "
        f"board, so naming one inside projects/{project}/ is a claim the "
        f"chapter contradicts:\n  " + "\n  ".join(offenders))
