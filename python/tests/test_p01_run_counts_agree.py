"""The run counts P01's README states must match the table it states them about.

WHY THIS EXISTS. That page carries one table of clock measurements, one row per
run, and then three prose statements about how many there were: a heading, a core
clock summary row and a reset clock summary row. The table is the record; the
three statements are written by hand.

On Tuesday 6 October 2026 the overview page in projects/README.md said the core
clock had been measured "seven times" and then listed six figures, for about an
hour, because a commit changed the number and did not add the figure. That is the
fourth hand-maintained list to drift in one day, after the test enumeration in
code.yml and the target table in docs/building.md, and the first where a count
and a list disagreed with each other rather than with reality.

WHAT IS CHECKED, all of it derived from the table rather than from a second list:

  - the heading's number equals the number of run rows
  - the reset clock row's number equals the number of run rows, because every
    run reports the reset clock
  - the core clock row's number equals the number of rows whose core column is
    not "not printed", because a second RESET press has twice truncated a report
    before its core figure appeared
  - each summary row lists exactly as many frequencies as its own number claims

THE COUNTS ARE SPELLED OUT IN WORDS on that page, which is the house style, so
the words are mapped here. A number this test cannot read is a failure and not a
skip: an unreadable count is exactly the one that stops being checked.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
README = ROOT / "projects" / "P01-toolchain-first-light" / "README.md"

WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
    "thirteen": 13, "fourteen": 14, "fifteen": 15, "sixteen": 16,
    "seventeen": 17, "eighteen": 18, "nineteen": 19, "twenty": 20,
    "thirty": 30, "forty": 40, "fifty": 50, "sixty": 60, "seventy": 70,
    "eighty": 80, "ninety": 90,
}

# The compound forms are SUMMED rather than listed, which is why the tens are in
# the table above and "twenty-one" is not. This stopped at twenty on Tuesday 6
# October 2026 and run 21 made the heading unreadable to this test, which is the
# correct failure and a tedious one to fix once per run. Splitting on the hyphen
# covers every form to ninety-nine and needs no further edits.

RUN_ROW = re.compile(r"^\|\s*run\s+(\d+)\s*\|([^|]*)\|", re.MULTILINE)
# [\w-]+ and not \w+, so that a hyphenated compound is captured whole. With
# \w+ the heading "measured twenty-one times" matched nothing at all, and the
# test reported a MISSING heading rather than an unreadable number, which sent
# the reader looking for the wrong fault.
HEADING = re.compile(r"^## The core clock, measured ([\w-]+) times", re.MULTILINE)
CORE_ROW = re.compile(
    r"\|\s*The core clock, \*\*measured ([\w-]+) times\*\*\s*\|([^|]*)\|")
RESET_ROW = re.compile(
    r"\|\s*The reset clock, \*\*measured ([\w-]+) times\*\*\s*\|([^|]*)\|")

# A frequency as that page writes one: digits in groups of three separated by
# spaces, optionally inside bold markers.
FREQUENCY = re.compile(r"\d{1,3}(?: \d{3})+")


def leading_list(cell: str) -> int:
    """How many frequencies the cell's OPENING list holds.

    Both summary rows carry more than one list of numbers. The core row names its
    eight core figures and then, later in the same cell, the eight implied input
    frequencies they divide down to. Counting every grouped number gives sixteen,
    which two earlier versions of this function did, and it failed on a correct
    page both times.

    The rule used instead is the magnitude. A list that one count describes is a
    list of one quantity, so its members have the same number of digits: the core
    figures are nine, the implied inputs are seven, the reset figures are eight.
    So this counts matches from the start of the cell for as long as they keep the
    digit count the first one had, and stops at the first that does not.

    Cutting on a phrase was tried first and does not work, because the figures are
    wrapped in bold markers and the text reads "Hz** against" rather than "Hz,".
    """
    found = FREQUENCY.findall(cell)
    if not found:
        return 0
    width = len(found[0].replace(" ", ""))
    count = 0
    for f in found:
        if len(f.replace(" ", "")) != width:
            break
        count += 1
    return count


def text() -> str:
    assert README.is_file(), "{} is missing".format(README)
    return README.read_text(encoding="utf-8")


def word(n: str, what: str) -> int:
    """A spelled-out count, including compounds such as twenty-one."""
    parts = n.split("-")
    assert all(p in WORDS for p in parts) and len(parts) <= 2, (
        "{} says {!r}, which this test cannot read as a number. Spell it as a "
        "word, or as tens-units with a hyphen; an unreadable count is the one "
        "that stops being checked.".format(what, n))
    total = sum(WORDS[p] for p in parts)
    if len(parts) == 2:
        tens, units = WORDS[parts[0]], WORDS[parts[1]]
        assert tens >= 20 and tens % 10 == 0 and 1 <= units <= 9, (
            "{} says {!r}, which is hyphenated but not tens-units. "
            "'twenty-one' is the shape this reads.".format(what, n))
    return total


def rows(t: str):
    found = RUN_ROW.findall(t)
    assert len(found) >= 3, (
        "found {} run rows in the measurement table, too few for this guard to "
        "mean anything".format(len(found)))
    return found


def test_the_runs_are_numbered_consecutively_from_one():
    """A missing or repeated run number would make every count below ambiguous."""
    numbers = [int(n) for n, _ in rows(text())]
    assert numbers == list(range(1, len(numbers) + 1)), (
        "the run rows are numbered {}, which is not 1 upward without gaps. "
        "Every count on this page is a count of these rows, so a gap makes all "
        "of them unreadable.".format(numbers))


def test_the_heading_counts_the_rows():
    t = text()
    m = HEADING.search(t)
    assert m, "no '## The core clock, measured N times' heading in " + str(README)
    said = word(m.group(1), "the heading")
    actual = len(rows(t))
    assert said == actual, (
        "the heading says {} runs and the table has {} rows".format(said, actual))


def test_the_reset_clock_row_counts_every_row():
    """Every run reports the reset clock, because it is printed before the raise."""
    t = text()
    m = RESET_ROW.search(t)
    assert m, "no reset clock summary row found"
    said = word(m.group(1), "the reset clock summary row")
    actual = len(rows(t))
    assert said == actual, (
        "the reset clock row says {} readings and the table has {} rows. Every "
        "run reports it, so these have to be equal.".format(said, actual))
    listed = leading_list(m.group(2))
    assert listed == said, (
        "the reset clock row says {} readings and lists {} frequencies".format(
            said, listed))


def test_the_core_clock_row_counts_only_the_rows_that_printed_one():
    """Two reports were truncated before their core figure, so this count is lower.

    The table writes those as "not printed" in the core column. Counting them
    would overstate the core readings by two, and counting every row as printed
    is the mistake this test exists to catch in the other direction.
    """
    t = text()
    m = CORE_ROW.search(t)
    assert m, "no core clock summary row found"
    said = word(m.group(1), "the core clock summary row")
    printed = [n for n, core in rows(t) if "not printed" not in core]
    assert said == len(printed), (
        "the core clock row says {} readings and {} of the {} table rows carry "
        "a core figure (runs {})".format(
            said, len(printed), len(rows(t)), ", ".join(printed)))
    listed = leading_list(m.group(2))
    assert listed == said, (
        "the core clock row says {} readings and lists {} frequencies. This is "
        "the exact defect the overview page carried on Tuesday 6 October 2026, "
        "where the number was changed and the figure was not.".format(
            said, listed))
