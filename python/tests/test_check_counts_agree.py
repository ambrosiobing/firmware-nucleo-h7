"""The check count stated in the READMEs must be the suite's actual size.

WHY THIS EXISTS. That one number has now drifted five times across three
documents. It read 87 in README.md and 123 in projects/README.md on Wednesday
7 October 2026 while the suite was 347, and both had been quoted in commit
messages as evidence of coverage. A figure used as evidence and maintained by
hand is a figure that drifts, which is the observation that already produced
python/tools/check_status.py, test_target_table_is_complete.py,
test_p01_run_counts_agree.py and the whole-directory run in code.yml.

HOW IT CHECKS, which is the part worth reading. The total comes from the live
collection, `request.session.items`, so it is the suite measuring itself rather
than a second hand-written number to keep in step. The documented figures are
parsed from the exact sentences that make the claim, and the parse asserts it
MATCHED rather than trusting silence: a reworded sentence has to fail loudly,
because a regular expression that quietly matches nothing passes while checking
nothing. That specific failure already happened once here, in
test_build_host_paths.py, and the answer was the same.

AND IT REFUSES TO JUDGE A SUBSET. Running one file collects a handful of checks,
and comparing that with the documented total would fail for a reason that is not
a defect. Below a floor it skips and says so.

WHAT IT DELIBERATELY DOES NOT CHECK. The pass and skip split cannot be known at
collection time, since no test has run yet. So the split is checked for internal
consistency only, that the documented passes plus the documented skips equal the
documented total, and the total is checked against reality. Claiming more than
that would need a reporter hook and would make this file part of the test
protocol rather than a test.
"""
import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]

# The floor below which a subset was obviously collected. Well under the real
# suite and well over any single file in it.
SUBSET_FLOOR = 100


def documented():
    """The three figures, each from the sentence that actually claims it."""
    root_readme = (ROOT / "README.md").read_text(encoding="utf-8")
    projects_readme = (ROOT / "projects" / "README.md").read_text(encoding="utf-8")

    # "347 checks on a runner with no board"
    badge = re.findall(r"(\d+) checks on a runner", root_readme)
    # "347 checks, of which 201 pass on a laptop"
    split = re.findall(r"(\d+) checks, of which (\d+) pass", projects_readme)
    # "the other 146 need a compiler"
    skips = re.findall(r"the other (\d+) need a compiler", projects_readme)

    assert len(badge) == 1, (
        "expected exactly one 'N checks on a runner' in README.md and found %d. "
        "If that sentence was reworded, this check stopped checking anything, "
        "which is worse than it failing" % len(badge))
    assert len(split) == 1, (
        "expected exactly one 'N checks, of which M pass' in projects/README.md "
        "and found %d" % len(split))
    assert len(skips) == 1, (
        "expected exactly one 'the other N need a compiler' in "
        "projects/README.md and found %d" % len(skips))

    return {
        "badge_total": int(badge[0]),
        "total": int(split[0][0]),
        "passes": int(split[0][1]),
        "skips": int(skips[0]),
    }


def test_the_documented_total_is_the_suite_size(request):
    """The suite measures itself, so this number cannot go stale quietly."""
    collected = len(request.session.items)
    if collected < SUBSET_FLOOR:
        pytest.skip(
            "only %d checks were collected, so a subset was run and the "
            "documented total cannot be compared with it. Run "
            "python -m pytest python/tests to check this" % collected)

    figures = documented()
    assert figures["total"] == collected, (
        "projects/README.md says %d checks and the suite collected %d. Update "
        "the figure rather than this test: it is quoted as evidence of coverage "
        "and has drifted five times already"
        % (figures["total"], collected))
    assert figures["badge_total"] == collected, (
        "README.md says %d checks on a runner and the suite collected %d"
        % (figures["badge_total"], collected))


def test_the_documented_split_adds_up():
    """Passes plus skips must be the total, whatever the total is.

    This one needs no collection and therefore runs even on a subset, which is
    why it is separate: an inconsistent split is a defect in the prose that is
    worth catching whichever way the suite was invoked.
    """
    figures = documented()
    assert figures["passes"] + figures["skips"] == figures["total"], (
        "projects/README.md says %d pass and %d skip, which is %d, against a "
        "stated total of %d"
        % (figures["passes"], figures["skips"],
           figures["passes"] + figures["skips"], figures["total"]))
