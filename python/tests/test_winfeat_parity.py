"""P16's window features and bounded tree, in C and Python, against one oracle.

**This file is chapter 16's criterion 1.** That criterion is "the equivalence
gate passes: all three implementations produce identical features and identical
classes on every window of the held-out set. The build fails if it does not", and
step 4 says to build the gate before any leg, "because doing it later never works
because by then each implementation has its own quiet assumption". Two of the
three implementations exist, the in-sensor one needs the shield, and the gate is
here and runs in the build.

**Everything is compared exactly.** The criterion is identity, not closeness, so
there is no tolerance anywhere in this file. All of it is integer arithmetic in
Q15, and the two implementations reach the integer square root by different
routes, `math.isqrt` in the Python and a bit-by-bit descent in the C, which is
what makes the comparison worth running.

**The windows are a recipe rather than a list.** A full window is 384 integers;
six of them would be two thousand numbers nobody reads. The oracle stores the
kind, the constants and any overridden sample, and `build` below turns that into
a window in the same eight lines the generator used.

**The finding is about the definition rather than about either
implementation.** Step 3 fixes the window length, the axes, the feature list and
the numeric format, and says nothing about rounding. In Q15 that is not enough
for a gate that demands equality: a mean rounded to the nearest value and a mean
truncated toward negative infinity differ by one least significant bit on most
windows, so two implementations each obeying the printed definition can fail
criterion 1. This project pins every rounding and a test runs the alternative to
show the disagreement.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import BUILD, require_artefact, shared_library_name

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P16-classifier-placement"
sys.path.insert(0, str(PROJECT / "python"))

from winfeat import (ERRORS, FEAT_AXES, FEAT_COUNT,  # noqa: E402
                      chapter_mean_nearest, classify as py_classify,
                      vector as py_vector, window as py_window)

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"
N = ORACLE["win_n"]


def build(spec):
    """The window builder, the same eight lines the oracle was generated with."""
    out = []
    for i in range(spec["n"]):
        for a in range(spec["axes"]):
            base = spec["const"][a]
            if spec["kind"] == "constant":
                v = base
            elif spec["kind"] == "square":
                v = base if (i % 2) == 0 else -base
            elif spec["kind"] == "ramp":
                v = base + i * spec["step"][a]
            else:
                raise AssertionError("unknown window kind " + spec["kind"])
            out.append(int(v))
    for key, value in spec.get("override", {}).items():
        out[int(key)] = int(value)
    return out


class CFeat(ctypes.Structure):
    """Must match feat_t in c/winfeat.h, field for field."""

    _fields_ = [("mean", ctypes.c_int16 * FEAT_AXES),
                ("rms", ctypes.c_int16 * FEAT_AXES),
                ("p2p", ctypes.c_int16 * FEAT_AXES)]


class CNode(ctypes.Structure):
    """Must match feat_node_t."""

    _fields_ = [("feature", ctypes.c_uint8), ("leaf", ctypes.c_uint8),
                ("threshold", ctypes.c_int16), ("left", ctypes.c_uint8),
                ("right", ctypes.c_uint8)]


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("winfeat")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        i16 = ctypes.POINTER(ctypes.c_int16)
        d.feat_window.restype = ctypes.c_int
        d.feat_window.argtypes = [i16, ctypes.c_uint32, ctypes.c_uint32,
                                  ctypes.POINTER(CFeat)]
        d.feat_vector.restype = ctypes.c_int
        d.feat_vector.argtypes = [ctypes.POINTER(CFeat), i16, ctypes.c_uint32]
        d.feat_classify.restype = ctypes.c_int
        d.feat_classify.argtypes = [ctypes.POINTER(CNode), ctypes.c_uint32,
                                    ctypes.c_uint32, i16, ctypes.c_uint32,
                                    ctypes.POINTER(ctypes.c_uint8)]
        _LIB = d
    return _LIB


def c_window(samples, n, axes):
    arr = (ctypes.c_int16 * max(len(samples), 1))(*samples) if samples \
        else (ctypes.c_int16 * 1)()
    out = CFeat()
    rc = c_lib().feat_window(arr, n, axes, ctypes.byref(out))
    return rc, out


def c_vector(feat):
    out = (ctypes.c_int16 * FEAT_COUNT)()
    rc = c_lib().feat_vector(ctypes.byref(feat), out, FEAT_COUNT)
    return rc, list(out)


def c_classify(tree, depth_max, features):
    nodes = (CNode * max(len(tree), 1))(
        *[CNode(n["feature"], n["leaf"], n["threshold"], n["left"], n["right"])
          for n in tree]) if tree else (CNode * 1)()
    feats = (ctypes.c_int16 * max(len(features), 1))(*features) if features \
        else (ctypes.c_int16 * 1)()
    out = ctypes.c_uint8(255)
    rc = c_lib().feat_classify(nodes, len(tree), depth_max, feats,
                               len(features), ctypes.byref(out))
    return rc, (out.value if rc == 0 else None)


@pytest.mark.parametrize("case", ORACLE["cases"], ids=lambda c: c["why"][:46])
def test_the_python_computes_every_feature_as_the_oracle_says(case):
    samples = build(case["spec"])
    rc, got = py_window(samples, case["spec"]["n"], case["spec"]["axes"])
    assert rc == 0, case["why"]
    for key in ("mean", "rms", "p2p"):
        assert got[key] == case[key], (key, case["why"])
    rc, vec = py_vector(got)
    assert rc == 0 and vec == case["vector"], case["why"]


@pytest.mark.parametrize("case", ORACLE["cases"], ids=lambda c: c["why"][:46])
def test_the_c_computes_every_feature_as_the_oracle_says(case):
    samples = build(case["spec"])
    rc, got = c_window(samples, case["spec"]["n"], case["spec"]["axes"])
    assert rc == 0, case["why"]
    for key in ("mean", "rms", "p2p"):
        assert list(getattr(got, key)) == case[key], (key, case["why"])
    rc, vec = c_vector(got)
    assert rc == 0 and vec == case["vector"], case["why"]


@pytest.mark.parametrize("case", ORACLE["shape_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_a_window_of_the_wrong_shape(case):
    samples = [0] * max(case["n"] * max(case["axes"], 1), 1)
    rc, _ = py_window(samples, case["n"], case["axes"])
    assert rc == ERRORS[case["error"]], case["why"]
    rc, _ = c_window(samples, case["n"], case["axes"])
    assert rc == ERRORS[case["error"]], case["why"]


@pytest.mark.parametrize("case", ORACLE["trees"], ids=lambda c: c["why"][:46])
def test_both_implementations_reach_the_same_class(case):
    rc, got = py_classify(case["tree"], case["depth_max"], case["features"])
    assert rc == 0 and got == case["klass"], case["why"]
    rc, got = c_classify(case["tree"], case["depth_max"], case["features"])
    assert rc == 0 and got == case["klass"], case["why"]


@pytest.mark.parametrize("case", ORACLE["tree_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_trees(case):
    rc, got = py_classify(case["tree"], case["depth_max"], case["features"])
    assert rc == ERRORS[case["error"]] and got is None, case["why"]
    rc, got = c_classify(case["tree"], case["depth_max"], case["features"])
    assert rc == ERRORS[case["error"]] and got is None, case["why"]


# ---------------------------------------------------------------------------
# The findings. Through the twin, so they run on the authoring laptop too.

def test_the_definition_does_not_pin_the_rounding_and_the_gate_needs_it():
    """Finding one, run against the rounding the chapter permits.

    One sample of minus one in an otherwise zero window: the sum is minus one
    over 128 samples. A floored mean is minus one and a mean rounded to the
    nearest value is zero. Step 3 requires neither, and step 4's gate demands
    equality, so two implementations each obeying the printed definition fail
    criterion 1 on this window.
    """
    case = next(c for c in ORACLE["cases"] if "THE FLOOR SHOWS" in c["why"])
    samples = build(case["spec"])
    column = [samples[i * FEAT_AXES] for i in range(N)]

    rc, got = py_window(samples, N, FEAT_AXES)
    assert rc == 0
    pinned = got["mean"][0]
    permitted = chapter_mean_nearest(column)
    assert pinned == case["mean"][0] == -1
    assert permitted == 0
    assert pinned != permitted, (
        "the two roundings now agree on this window, so it no longer "
        "demonstrates the finding and a different window is needed")
    print("\n  one sample of minus one over {} samples: the pinned mean is {} "
          "and the rounding step 3 also permits gives {}, and criterion 1 "
          "compares them for equality".format(N, pinned, permitted))


def test_the_depth_bound_is_what_makes_the_latency_bounded():
    """Finding three, against a tree that cycles.

    Step 5 picks a bounded-depth tree because bounded depth is bounded latency.
    A walk that trusts its own child indices is not bounded: this tree sends
    every path back to its own parent, and without the bound the walk never
    ends. On the board that is a missed control deadline rather than a wrong
    class, which is why it is a refusal and not a default.
    """
    case = next(c for c in ORACLE["tree_refusals"] if "CYCLES" in c["why"])
    rc, got = py_classify(case["tree"], case["depth_max"], case["features"])
    assert rc == ERRORS["depth"] and got is None
    # And the same tree with a leaf added terminates, which is the control: the
    # refusal is about the cycle and not about the tree being small.
    fixed = [dict(case["tree"][0]), {"feature": 1, "leaf": 1, "threshold": 0,
                                     "left": 1, "right": 1}]
    rc, got = py_classify(fixed, case["depth_max"], case["features"])
    assert rc == 0 and got == 1
    print("\n  a tree whose children point back at their parent is refused at "
         "the depth bound; the same tree with a leaf returns class 1")
