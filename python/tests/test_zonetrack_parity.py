"""P15's hysteresis, the C and the Python against one oracle read from a figure.

**Where the oracle comes from.** Chapter 15's figure 15.4 draws four edges with
their conditions, and its step 9 sketch gives two of the three thresholds and the
confirmation count. The figure is the more complete source and the oracle follows
it. Nineteen cases, five of which expect a refusal.

**What was checked before either implementation was allowed to agree.** That every
state change in every case is one of the figure's four edges or one of the two this
project documents as its own decisions, and that no confirmed transition happens
sooner than the confirmation count allows. Neither check consults an
implementation.

**Why the thresholds are parameters.** The figure's own note says the numbers are
"a starting point measured against a real target rather than constants to be
trusted", so a file that compiled them in would disagree with the chapter the first
time a measurement moved one.

**What this does not cover.** The validity filter, the reduction and the status
byte, because the chapter specifies none of the six constants they need and this
project will not invent them. They are bench decisions by the chapter's own
account.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import BUILD, require_artefact, shared_library_name

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P15-time-of-flight"
sys.path.insert(0, str(PROJECT / "python"))

from zonetrack import (ERRORS, fresh_tracker, state_name,  # noqa: E402
                       update as py_update)


class CPolicy(ctypes.Structure):
    """Must match zonetrack_policy_t in c/zonetrack.h, field for field."""

    _fields_ = [
        ("far_mm", ctypes.c_uint16),
        ("enter_mm", ctypes.c_uint16),
        ("leave_mm", ctypes.c_uint16),
        ("confirm_n", ctypes.c_uint8),
        ("confirm_m", ctypes.c_uint8),
    ]


class CTracker(ctypes.Structure):
    """Must match zonetrack_t in the same header, field for field."""

    _fields_ = [
        ("state", ctypes.c_int),
        ("votes", ctypes.c_uint16),
        ("seen", ctypes.c_uint8),
        ("frames", ctypes.c_uint32),
        ("changes", ctypes.c_uint32),
        ("ready", ctypes.c_bool),
    ]


def oracle():
    loaded = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
    assert loaded["schema"] == 1, "the oracle's schema moved"
    assert loaded["cases"], "an empty oracle proves nothing"
    return loaded


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("zonetrack")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        d.zonetrack_init.restype = ctypes.c_int
        d.zonetrack_init.argtypes = [ctypes.POINTER(CTracker)]
        d.zonetrack_update.restype = ctypes.c_int
        d.zonetrack_update.argtypes = [ctypes.POINTER(CTracker),
                                       ctypes.POINTER(CPolicy),
                                       ctypes.c_uint16]
        d.zonetrack_state_name.restype = ctypes.c_char_p
        d.zonetrack_state_name.argtypes = [ctypes.c_int]
        _LIB = d
    return _LIB


def policy_of(case, loaded):
    return case.get("policy", loaded["default_policy"])


def c_run(case, loaded):
    lib = c_lib()
    p = policy_of(case, loaded)
    pol = CPolicy(p["far_mm"], p["enter_mm"], p["leave_mm"],
                  p["confirm_n"], p["confirm_m"])
    t = CTracker()
    assert lib.zonetrack_init(ctypes.byref(t)) == 0
    out = []
    for d in case["distances"]:
        out.append(lib.zonetrack_update(ctypes.byref(t), ctypes.byref(pol), d))
    return out, t


def py_run(case, loaded):
    p = policy_of(case, loaded)
    t = fresh_tracker()
    return [py_update(t, p, d) for d in case["distances"]], t


CASES = oracle()["cases"]


@pytest.mark.parametrize("case", CASES, ids=lambda c: c["why"][:48])
def test_the_python_twin_matches_the_oracle(case):
    loaded = oracle()
    results, t = py_run(case, loaded)
    if "error" in case:
        assert all(r == ERRORS[case["error"]] for r in results), case["why"]
        assert t["frames"] == 0, "a refused frame must not be counted"
        return
    assert [state_name(r) for r in results] == case["states"], case["why"]
    assert t["frames"] == len(case["distances"])


@pytest.mark.parametrize("case", CASES, ids=lambda c: c["why"][:48])
def test_the_c_matches_the_oracle(case):
    loaded = oracle()
    results, t = c_run(case, loaded)
    if "error" in case:
        assert all(r == ERRORS[case["error"]] for r in results), case["why"]
        assert t.frames == 0, "a refused frame must not be counted"
        return
    assert [state_name(r) for r in results] == case["states"], case["why"]
    assert t.frames == len(case["distances"])


def test_the_two_implementations_agree_on_every_case():
    """Pairwise, which the oracle tests do not do: a case the oracle happens not
    to constrain cannot hide a difference between the two."""
    loaded = oracle()
    for case in CASES:
        c_out, c_t = c_run(case, loaded)
        p_out, p_t = py_run(case, loaded)
        assert c_out == p_out, case["why"]
        assert c_t.frames == p_t["frames"], case["why"]
        assert c_t.changes == p_t["changes"], case["why"]
        assert bool(c_t.ready) == p_t["ready"], case["why"]


def test_the_state_names_agree_between_the_two_and_cover_the_invalid_case():
    lib = c_lib()
    for value in range(-2, 6):
        assert lib.zonetrack_state_name(value).decode() == state_name(value), value
    assert lib.zonetrack_state_name(99).decode() == "invalid"


def test_a_target_sitting_between_the_thresholds_never_changes_state():
    """The property the hysteresis exists for, over a long run rather than a case.

    A target hovering between enter and leave is the situation a single
    threshold handles worst: it chatters once per frame. This drives 600 frames
    of a target oscillating across the gap and asserts the state changes exactly
    once, on the way in, and never again.
    """
    loaded = oracle()
    p = loaded["default_policy"]
    lib = c_lib()
    pol = CPolicy(p["far_mm"], p["enter_mm"], p["leave_mm"],
                  p["confirm_n"], p["confirm_m"])
    t = CTracker()
    lib.zonetrack_init(ctypes.byref(t))

    for _ in range(p["confirm_n"] + 1):          # get it into present first
        lib.zonetrack_update(ctypes.byref(t), ctypes.byref(pol), 500)
    assert t.state == 2, "the walk did not reach present"
    changes_then = t.changes

    for i in range(600):
        near = 900 if (i % 2 == 0) else 850      # both inside the gap
        lib.zonetrack_update(ctypes.byref(t), ctypes.byref(pol), near)

    assert t.state == 2, "a target inside the gap moved the state"
    assert t.changes == changes_then, (
        "{} state changes in 600 frames inside the hysteresis gap".format(
            t.changes - changes_then))
    print("\n  600 frames across the 800 to 950 gap, 0 state changes")


def test_a_single_threshold_would_chatter_and_this_is_why_there_are_two():
    """The falsifier for the test above, so it is not vacuous.

    With enter and leave one millimetre apart the policy still passes its sanity
    check but has almost no hysteresis left, and the same oscillating target
    moves the state repeatedly. If this did not chatter, the test above would be
    proving nothing about the gap.
    """
    lib = c_lib()
    pol = CPolicy(1500, 899, 900, 1, 1)
    t = CTracker()
    lib.zonetrack_init(ctypes.byref(t))
    for i in range(50):
        lib.zonetrack_update(ctypes.byref(t), ctypes.byref(pol),
                             1000 if (i % 2 == 0) else 500)
    assert t.changes > 10, (
        "a policy with a 1 mm gap did not chatter, so the gap test is vacuous: "
        "{} changes".format(t.changes))
    print("  the same target with a 1 mm gap: {} state changes".format(t.changes))
