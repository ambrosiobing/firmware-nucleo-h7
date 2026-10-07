"""check_link_closure.py's two blind spots, both closed Wednesday 7 October 2026.

WHY THIS FILE EXISTS AT ALL. That check reported "0 not closed" while two of
P06's three back ends had never been read and no `c/instr` include from any
project file had ever been followed. It was not wrong about what it looked at.
It was quiet about what it did not look at, and a check that is quiet about its
own blind spot reads exactly like a check that found nothing wrong.

Both defects were found by using the check on a real change rather than by
reading it, and both would regress silently, which is the whole argument for
pinning them here. A mutation of either one puts the tool back to reporting
success over an unexamined target.

THE TWO DEFECTS, in the order they were found:

  a set() value holding ${...}   the value pattern was `[^)]*`, which TERMINATES
                                AT THE FIRST ")" and so stopped inside
                                "acq_${BACKEND" . No .c path was found in that
                                fragment, so the set() contributed nothing and
                                the only back end ever followed was the one
                                whose set() happens to hold a literal path
  a cross-directory include      a quoted header was looked for beside the
                                including file and at the repository root only,
                                so projects/P06-timer-sampling/c/acq_timer.c
                                could not reach c/instr/adcmath.h. The one case
                                that did work is c/instr/freqcount.c reaching
                                c/instr/lseref.h, which is same-directory, and
                                that is the case the check was written for
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python" / "tools"))

import check_link_closure as closure  # noqa: E402


def cmake_text():
    return (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")


def test_a_loop_variable_is_read_with_all_of_its_words():
    """foreach(BACKEND systick timer dma) supplies three words, not one."""
    loops = closure.foreach_values(cmake_text())
    assert "BACKEND" in loops, (
        "the P06 back end loop was not found at all, so no set() inside it can "
        "be expanded and two of three back ends go unread")
    assert set(loops["BACKEND"]) >= {"systick", "timer", "dma"}, (
        "expected all three back end words, got %r" % (loops["BACKEND"],))


def test_the_two_back_ends_that_were_invisible_are_now_listed():
    """THE FIRST DEFECT, as an assertion about the two files it hid.

    ACQ is set three times, once per back end. Only the dma one holds a literal
    path; the other two interpolate ${BACKEND}. So before the fix this variable
    resolved to exactly one file and the check followed one third of P06.
    """
    text = cmake_text()
    loops = closure.foreach_values(text)
    variables, unresolved = closure.source_variables(text, loops)

    assert "ACQ" in variables, "the acquisition back end variable resolved to nothing"
    names = {pathlib.PurePosixPath(p).name for p in variables["ACQ"]}
    for expected in ("acq_timer.c", "acq_systick.c"):
        assert expected in names, (
            "%s is not in the resolved ACQ set, which is the defect: its "
            "includes were never followed. Got %r" % (expected, sorted(names)))
    assert not unresolved, (
        "a set() value still holds an unexpanded ${...}: %r. Unresolved is "
        "reported rather than skipped on purpose, so this failing means the "
        "report is working, not that it is safe to ignore" % (unresolved,))


def test_a_header_in_another_first_party_directory_resolves():
    """THE SECOND DEFECT. The search path has to be the compiler's.

    acq_timer.c includes "adcmath.h", which lives in c/instr. The target passes
    c/instr in its INCLUDES, so the compiler finds it and this check must too.
    """
    including = ROOT / "projects" / "P06-timer-sampling" / "c" / "acq_timer.c"
    assert including.exists(), "the file this test is about has moved"

    with_path = closure.header_to_source("adcmath.h", including, ["c/instr"])
    assert with_path == "c/instr/adcmath.c", (
        "adcmath.h did not resolve to its .c with c/instr on the include path, "
        "got %r" % (with_path,))

    # And without the include path it does not resolve, which is the old
    # behaviour written down rather than described. This is not a wish: it is
    # why the include directories had to be threaded through.
    without = closure.header_to_source("adcmath.h", including, [])
    assert without is None, (
        "expected no resolution without the include path, got %r. If this "
        "starts resolving, the fix has been replaced by something broader and "
        "the reason for it is no longer recorded anywhere" % (without,))


def test_the_check_is_not_vacuous():
    """It cannot pass by finding nothing to check.

    Every other guard in this repository carries one of these, after a parity
    test passed on a laptop where nothing had been built.
    """
    text = cmake_text()
    loops = closure.foreach_values(text)
    variables, _ = closure.source_variables(text, loops)

    assert len(loops) >= 1, "no foreach loops found in CMakeLists.txt"
    assert variables, "no set() variable naming a .c file found"
    assert "add_firmware(" in text, (
        "no add_firmware call in CMakeLists.txt, so this file is asserting "
        "things about a build that is not there")
