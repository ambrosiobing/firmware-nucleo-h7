"""Every test file has to be run by CI, and for three days four were not.

WHY THIS EXISTS. On Tuesday 6 October 2026, while wiring test_pwmmath.py in, the
workflow turned out to name its test files one at a time, and four of them were
missing from the list:

  test_clocktree.py                      added Sunday 4 October 2026
  test_clocktree_parity.py               added Sunday 4 October 2026
  test_freqmath.py                       added Monday 5 October 2026
  test_project_files_are_documented.py   added Monday 5 October 2026

Every one of those arrived in a commit that said it added coverage, and every one
of them had never run on a push. The clock tree decode's eight refusals, P01's
four-language parity, the frequency counter's arithmetic and the guard written
after a README section was deleted by a scripted edit were all unprotected.

A test that exists and never runs is not coverage, and the failure mode is
silent in the worst way: the file is there, it passes locally, and the badge is
green for reasons that have nothing to do with it.

THE FIX IS NOT A FIFTH ENTRY IN THE LIST. code.yml now ends the checks job with a
run of the whole directory, so a new file is picked up the moment it exists. This
file is the guard on that guard: it asserts the whole-directory step is still
there, so a future tidy-up that removes it turns the suite red instead of quietly
restoring the hole.

It deliberately does NOT require every file to be named individually. The named
steps are worth keeping for the log, and requiring both would make adding a test
a two-file change again, which is the thing that failed.
"""
from __future__ import annotations

import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
WORKFLOW = ROOT / ".github" / "workflows" / "code.yml"
TESTS = ROOT / "python" / "tests"

# The run line that makes the enumeration unable to be wrong. Matched loosely on
# purpose: any pytest invocation over the whole tests directory counts, so the
# step can be renamed or moved without this test caring.
WHOLE_SUITE = re.compile(r"pytest\s+python/tests\s", re.MULTILINE)


def test_the_workflow_exists():
    assert WORKFLOW.is_file(), (
        "{} is missing, so nothing here can say what CI runs".format(WORKFLOW))


def test_ci_runs_the_whole_tests_directory():
    text = WORKFLOW.read_text(encoding="utf-8")
    assert WHOLE_SUITE.search(text), (
        "code.yml no longer runs the whole python/tests directory anywhere. "
        "Without that step the workflow is back to naming test files one at a "
        "time, which is how test_clocktree.py, test_clocktree_parity.py, "
        "test_freqmath.py and test_project_files_are_documented.py went three "
        "days without ever running on a push. If the step was removed on "
        "purpose, every file in python/tests has to be named individually "
        "instead, and this test should be replaced by one that checks that.")


def test_there_are_test_files_to_run():
    """Guards against the guard passing vacuously.

    If python/tests were empty or renamed, the test above would still pass on a
    workflow that runs an empty directory, and this file would be reassuring
    about nothing. Three is the same floor assert_not_vacuous uses in conftest.
    """
    found = sorted(p.name for p in TESTS.glob("test_*.py"))
    assert len(found) >= 3, (
        "found {} test files in {}, which is too few for this guard to mean "
        "anything: {}".format(len(found), TESTS, found))


def test_every_test_file_is_importable_by_pytest():
    """A file pytest cannot collect is as unrun as one nobody named.

    The whole-directory step only helps if pytest can actually collect what it
    finds. This catches the narrow case of a test file whose name pytest does
    not match, which would make it invisible to the directory run while looking
    like a test to a reader.
    """
    stray = sorted(
        p.name for p in TESTS.glob("*.py")
        if p.name not in {"conftest.py", "__init__.py"}
        and not p.name.startswith("test_")
    )
    if stray:
        pytest.fail(
            "these files in python/tests are not named test_*.py, so the "
            "whole-directory run will not collect them: " + repr(stray)
            + ". Rename them, or move them to python/tools if they are not "
            "tests.")
