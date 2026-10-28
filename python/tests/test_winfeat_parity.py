"""P16's window features and bounded tree, in four languages, against one oracle.

**This file is chapter 16's criterion 1.** That criterion is "the equivalence
gate passes: all three implementations produce identical features and identical
classes on every window of the held-out set. The build fails if it does not", and
step 4 says to build the gate before any leg, "because doing it later never works
because by then each implementation has its own quiet assumption".

**The chapter's three implementations are the three placements**, in the sensor,
on the microcontroller and on the host, and only the host one exists. What runs
here is that one written in four languages, compared the way the placement gate
will be, and none of it needed the shield.

**Everything is compared exactly.** The criterion is identity, not closeness, so
there is no tolerance anywhere in this file. All of it is integer arithmetic in
Q15, and the four implementations reach the integer square root by four
different routes, a bit-by-bit descent in the C, `math.isqrt` in the Python,
`u64::isqrt` in the Rust and a corrected `std::sqrt` in the C++, which is what
makes the comparison worth running and is what found the saturation defect
recorded further down this file.

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
import math
import sys
from pathlib import Path

import pytest

from conftest import (BUILD, BUILD_HOST_CMD, FEAT_CPP_FILTER,
                      FEAT_RUST_FILTER, require_artefact, run_filter,
                      shared_library_name)

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


def test_the_root_mean_square_saturates_and_the_gate_found_that():
    """The corner the third implementation found, run through the twin.

    Every sample at negative full scale: the mean of the squares is 1073741824
    and its root is exactly 32768, one above what Q15 holds. Until Tuesday 27
    October 2026 the C cast that root to int16_t and produced negative 32768
    while the Python returned 32768, which is not a Q15 value at all, and no
    oracle case reached the corner, so criterion 1's gate had never compared the
    two there. All three implementations now saturate at 32767, which is what
    peak to peak already did and for the same stated reason.
    """
    case = next(c for c in ORACLE["cases"] if "SATURATES" in c["why"])
    samples = build(case["spec"])
    rc, got = py_window(samples, N, FEAT_AXES)
    assert rc == 0
    column = [samples[i * FEAT_AXES] for i in range(N)]
    unsaturated = math.isqrt(sum(v * v for v in column) >> 7)
    assert unsaturated == 32768, (
        "the root no longer exceeds Q15 on this window, so it no longer "
        "demonstrates the finding and a different window is needed")
    assert got["rms"][0] == 32767 == case["rms"][0]
    assert got["mean"][0] == -32768, "the mean itself is exactly representable"
    print("\n  every sample at negative full scale: the root of the mean of "
          "the squares is {} and Q15 holds {}, so it saturates; the mean of "
          "the same window is {} and needs no saturation"
          .format(unsaturated, got["rms"][0], got["mean"][0]))


# ---------------------------------------------------------------------------
# The Rust, driven as a filter over the same oracle. One process for the whole
# run, which is conftest's rule.
#
# Two verbs, counts before data:
#
#     F <n> <axes> <n*axes samples>
#     C <nodes> <depth_max> <nfeat> <5 values per node> <nfeat features>
#
# An F request is 386 tokens, which is why the filter reads whole lines.


def feature_request(spec):
    samples = build(spec)
    return "F {} {} {}".format(spec["n"], spec["axes"],
                               " ".join(str(v) for v in samples)).strip()


def tree_request(tree, depth_max, features):
    out = ["C", str(len(tree)), str(depth_max), str(len(features))]
    for node in tree:
        out += [str(node["feature"]), str(node["leaf"]),
                str(node["threshold"]), str(node["left"]), str(node["right"])]
    out += [str(v) for v in features]
    return " ".join(out)


def fields_of(answer):
    out = {}
    for token in answer.split():
        key, value = token.split("=", 1)
        out[key] = [int(v) for v in value.split(",")]
    return out


# The C++ and the Rust speak one protocol, so the test below runs over both and
# the language is in the test id, which is what names the one that disagreed.
FILTERS = [(FEAT_CPP_FILTER, "cpp", BUILD_HOST_CMD),
           (FEAT_RUST_FILTER, "rust", "cargo build --release --workspace")]
FILTER_ARGS = [(path, command) for path, _, command in FILTERS]
FILTER_IDS = [name for _, name, _ in FILTERS]


@pytest.mark.parametrize("path,command", FILTER_ARGS, ids=FILTER_IDS)
def test_each_filter_answers_every_case_as_the_oracle_says(path, command):
    """One process, every case of all four kinds, in the oracle's order."""
    require_artefact(path, command)
    lines = []
    for case in ORACLE["cases"]:
        lines.append(feature_request(case["spec"]))
    for case in ORACLE["shape_refusals"]:
        spec = {"kind": "constant", "n": case["n"], "axes": case["axes"],
                "const": [0] * max(case["axes"], 1)}
        lines.append(feature_request(spec))
    for case in ORACLE["trees"] + ORACLE["tree_refusals"]:
        lines.append(tree_request(case["tree"], case["depth_max"],
                                  case["features"]))
    answers = run_filter(path, lines)
    assert len(answers) == len(lines)

    at = 0
    for case in ORACLE["cases"]:
        got = fields_of(answers[at])
        for key in ("mean", "rms", "p2p"):
            assert got[key] == case[key], (key, case["why"])
        assert got["vector"] == case["vector"], case["why"]
        at += 1
    for case in ORACLE["shape_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        at += 1
    for case in ORACLE["trees"]:
        assert answers[at] == "class={}".format(case["klass"]), case["why"]
        at += 1
    for case in ORACLE["tree_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        at += 1
    assert at == len(lines)
    print("\n  the filter answered all {} requests, {} windows and {} trees, "
          "including {} refusals"
          .format(len(lines), len(ORACLE["cases"]) + len(ORACLE["shape_refusals"]),
                  len(ORACLE["trees"]) + len(ORACLE["tree_refusals"]),
                  len(ORACLE["shape_refusals"]) + len(ORACLE["tree_refusals"])))


# ---------------------------------------------------------------------------
# The C++ reaches the floored integer square root a fourth way, and this is the
# claim its header makes, run rather than asserted. Through a replica of that
# algorithm in Python, so it runs on the authoring laptop where nothing
# compiles.


def cpp_isqrt(v):
    """The C++ algorithm from cpp/winfeat.hpp, with the loop counts.

    Returns (root, downward corrections, upward corrections). Python's
    math.sqrt is the same IEEE double square root the C++ calls.
    """
    if v == 0:
        return 0, 0, 0
    r = int(math.sqrt(float(v)))
    if r > 0xFFFFFFFF:
        r = 0xFFFFFFFF
    down = up = 0
    while r > 0 and r * r > v:
        r -= 1
        down += 1
    while r < 0xFFFFFFFF and (r + 1) * (r + 1) <= v:
        r += 1
        up += 1
    return r, down, up


def test_the_cpp_route_needs_no_correction_anywhere_in_this_projects_range():
    """The finding in cpp/winfeat.hpp, and it corrects the C header's reason.

    `c/winfeat.c` writes its root as a bit-by-bit descent because "a float sqrt
    followed by a cast would agree almost always, and almost always is what an
    exact gate exists to refuse". That is right about a function typed uint64_t
    and it is not right about this window's arithmetic: the mean of the squares
    cannot exceed two to the thirtieth, and over every adversarial input in
    that range the uncorrected cast is already exact. So the C++ corrects a
    root that never needs correcting here, and the refusal in the C header is
    justified by the type rather than by the inputs.
    """
    top = 1 << 30
    cases = {0, 1, 2, 3, top - 1, top}
    for k in range(0, 32769):
        for v in (k * k - 1, k * k, k * k + 1):
            if 0 <= v <= top:
                cases.add(v)
    corrected = []
    for v in cases:
        got, down, up = cpp_isqrt(v)
        assert got == math.isqrt(v), (v, got)
        if down or up:
            corrected.append(v)
    assert not corrected, (
        "the corrected sqrt now needs its correction inside this project's "
        "range, so the header's finding has stopped being true: {}"
        .format(corrected[:4]))
    print("\n  {} adversarial inputs up to 2**30, every one exact, and the "
          "uncorrected cast needed no correction on any of them"
          .format(len(cases)))


def test_the_correction_is_not_dead_code_outside_that_range():
    """The control, so the loops are shown to be reachable rather than assumed.

    The shape that defeats a sqrt and a cast is v equal to k squared less one,
    where the true root sits a hair below a whole number and the double rounds
    up to it. That needs the hair to be narrower than half a unit of least
    precision, which first happens at k equal to two to the twenty-sixth. The
    smallest such input is named here, eleven binary orders of magnitude above
    anything this project produces.
    """
    k = (1 << 26) + 1
    v = k * k - 1
    assert v == 4503599761588224, "the named smallest failing input moved"
    uncorrected = int(math.sqrt(float(v)))
    corrected, down, up = cpp_isqrt(v)
    assert uncorrected == k, "the cast no longer rounds up here"
    assert corrected == math.isqrt(v) == k - 1
    assert down == 1 and up == 0

    # And the clamp, which is the other reachable branch: the sqrt of the
    # largest uint64_t rounds up to two to the thirty-second, and without the
    # clamp the second loop's product wraps to zero and walks upward for ever.
    big = (1 << 64) - 1
    assert int(math.sqrt(float(big))) == 1 << 32
    clamped, down, up = cpp_isqrt(big)
    assert clamped == math.isqrt(big) == 0xFFFFFFFF
    assert down == 0 and up == 0
    print("\n  {} is {} squared less one: the cast gives {} and the answer is "
          "{}, corrected downward once; and at the top of a 64-bit input the "
          "cast gives {} where the answer is {}, which the clamp holds"
          .format(v, k, uncorrected, corrected, 1 << 32, clamped))
