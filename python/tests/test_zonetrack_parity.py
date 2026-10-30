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

from conftest import (BUILD, ZONE_RUST_FILTER, require_artefact,
                      run_filter, shared_library_name)

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


# ---------------------------------------------------------------------------
# The Rust, driven as a filter over the same oracle. One process for the whole
# run, which is conftest's rule.
#
# One verb, because this project is one state machine fed a sequence:
#
#     T <far> <enter> <leave> <confirm_n> <confirm_m> <n> <distance>*n
#         -> frames=N changes=N ready=0|1 states=clear,approaching,..
#         -> REFUSED policy
#
# THE POLICY TRAVELS IN EVERY REQUEST, because it is a parameter in all four
# implementations and six of the nineteen cases override the chapter's own
# numbers. A filter holding a policy between lines would answer a request that
# never named one.
#
# THE WHOLE SEQUENCE OF STATES COMES BACK, not the final one, because a state
# machine that reaches the right end through the wrong middle is wrong. Three
# cases exist to pin a middle: the window cleared on an edge, a target that
# returns while leaving, and a hover between the thresholds that confirms
# nothing.
#
# AND changes IS CHECKED AGAINST THE ORACLE'S OWN SEQUENCE rather than against
# a number written beside it. The chapter says the change count is the figure a
# policy is judged by; here it is derived from the states the oracle states, by
# counting adjacent pairs that differ and adding one if the first state is not
# clear, since every tracker starts clear. Neither implementation can satisfy
# that by accident.

NEWLINE = chr(10)


def track_request(case, loaded):
    p = policy_of(case, loaded)
    parts = ["T", str(p["far_mm"]), str(p["enter_mm"]), str(p["leave_mm"]),
             str(p["confirm_n"]), str(p["confirm_m"]),
             str(len(case["distances"]))]
    parts += [str(d) for d in case["distances"]]
    return " ".join(parts)


def all_requests(loaded):
    """Every request, in the oracle's own order.

    Built where a test with no cargo can reach them, because the filter test
    SKIPS where there is no cargo and a skipped body never runs at all.
    """
    return [track_request(c, loaded) for c in loaded["cases"]]


def fields_of(answer):
    out = {}
    for token in answer.split():
        key, value = token.split("=", 1)
        out[key] = value
    return out


def changes_in(states):
    """The state changes a sequence of state names implies.

    Every tracker starts clear, so a first state that is not clear is itself a
    change, and after that every adjacent pair that differs is one.
    """
    if not states:
        return 0
    total = 1 if states[0] != "clear" else 0
    for before, after in zip(states, states[1:]):
        if before != after:
            total += 1
    return total


def test_every_request_says_what_its_case_says_without_running_anything():
    """Runs everywhere, including where no filter exists, on purpose."""
    loaded = oracle()
    lines = all_requests(loaded)
    assert len(lines) == len(loaded["cases"])
    overrides = 0
    for case, line in zip(loaded["cases"], lines):
        t = line.split()
        p = policy_of(case, loaded)
        assert t[0] == "T", case["why"]
        assert int(t[1]) == p["far_mm"], case["why"]
        assert int(t[2]) == p["enter_mm"], case["why"]
        assert int(t[3]) == p["leave_mm"], case["why"]
        assert int(t[4]) == p["confirm_n"], case["why"]
        assert int(t[5]) == p["confirm_m"], case["why"]
        assert int(t[6]) == len(case["distances"]), case["why"]
        assert [int(x) for x in t[7:]] == case["distances"], case["why"]
        if "policy" in case:
            overrides += 1
            assert p != loaded["default_policy"], (
                "a case that names a policy must differ from the default, or "
                "it is not testing an override: {}".format(case["why"]))
    assert overrides == sum(1 for c in loaded["cases"] if "policy" in c)
    assert overrides >= 6, (
        "six of the cases override the chapter's numbers, and {} were built "
        "with an override".format(overrides))
    print(NEWLINE + "  {} requests built with no filter and no compiler, {} of "
          "them carrying a policy of their own".format(len(lines), overrides))


def test_the_rust_filter_answers_every_case_as_the_oracle_says():
    """One process, every case in the oracle's order, refusals included."""
    require_artefact(ZONE_RUST_FILTER, "cargo build --release --workspace")
    loaded = oracle()
    lines = all_requests(loaded)
    answers = run_filter(ZONE_RUST_FILTER, lines)
    assert len(answers) == len(lines)

    refusals = 0
    for case, answer in zip(loaded["cases"], answers):
        if "error" in case:
            assert answer == "REFUSED {}".format(case["error"]), case["why"]
            refusals += 1
            continue
        got = fields_of(answer)
        states = got["states"].split(",")
        assert states == case["states"], case["why"]
        assert int(got["frames"]) == len(case["distances"]), case["why"]
        # The change count, derived from the oracle's own states rather than
        # read from a field beside them.
        assert int(got["changes"]) == changes_in(case["states"]), case["why"]
        p = policy_of(case, loaded)
        # ready says a full window has been seen since the last edge, so it
        # cannot be set unless at least that many frames have passed.
        if int(got["ready"]) == 1:
            assert len(case["distances"]) >= p["confirm_m"], case["why"]
    assert refusals == sum(1 for c in loaded["cases"] if "error" in c)
    assert refusals >= 5, (
        "five of the cases are policy refusals and {} were seen"
        .format(refusals))
    print(NEWLINE + "  the Rust answered all {} cases, {} of them refusals, "
          "with every state in every sequence compared by name and the change "
          "count derived from the oracle".format(len(answers), refusals))


def test_the_rust_chatters_under_one_threshold_and_not_under_two():
    """Not a parity check: why the hysteresis exists, through the filter.

    A policy whose enter and leave thresholds are one millimetre apart is the
    nearest this file can get to the single threshold the chapter warns about,
    and a target hovering there must change state far more often than the same
    target under the chapter's own numbers. The comparison is a count rather
    than an argument.
    """
    require_artefact(ZONE_RUST_FILTER, "cargo build --release --workspace")
    loaded = oracle()
    d = loaded["default_policy"]
    hover = [870, 880, 870, 880, 870, 880, 870, 880, 870, 880, 870, 880]
    tight = {"far_mm": d["far_mm"], "enter_mm": 875, "leave_mm": 876,
             "confirm_n": 1, "confirm_m": 1}
    lines = [track_request({"distances": hover}, loaded),
             track_request({"distances": hover, "policy": tight}, loaded)]
    answers = run_filter(ZONE_RUST_FILTER, lines)
    wide = int(fields_of(answers[0])["changes"])
    narrow = int(fields_of(answers[1])["changes"])
    assert narrow > wide, (
        "thresholds one millimetre apart must chatter more than the "
        "chapter's, and they changed {} times against {}"
        .format(narrow, wide))
    print(NEWLINE + "  the same hovering target changes state {} times under "
          "the chapter's thresholds and {} times with them one millimetre "
          "apart".format(wide, narrow))
