"""P03's four attributions against one list of synthetic steps.

**Which half of P03 this is.** The receive path needs a peripheral and refuses
until RM0455 is read. The attribution does not: it is arithmetic over counters a
run recorded, and it is the half where a wrong answer publishes a wrong
conclusion rather than merely failing to work. So the attribution gets four
implementations and the receive path stays C and keeps refusing.

**The oracle is synthetic steps whose answers are known by construction**, the
same ones `python/tests/test_report.py` already holds the Python to. The
interesting ones are not the clean cases:

  the bridge case     a tenth of the traffic never reached the peripheral, the
                      target lost nothing, and reporting it as target loss would
                      be the wrong conclusion. This is why the file exists.
  overrun alone       a latency failure. A larger ring would not help.
  drops alone         a throughput failure. A shorter handler would not help.
  both                still not summed, because the fixes are opposite.
  over-accounted      the target claims more bytes than the host sent, which is a
                      defect in the measurement and is refused rather than
                      clamped.

**`first_loss` is in the comparison too**, because it is where the confusion
would actually be published: a ramp whose bridge gives up at a high rate must not
read as the target failing there. All four skip bridge rows and all four take the
lowest rate rather than the first row, which an unsorted ramp distinguishes.

C is the reference, as in the other parity tests, because it is the
implementation that compiles for the target.
"""
from __future__ import annotations

import ctypes
import sys
from pathlib import Path

import pytest

from conftest import (
    ATTR_CPP_FILTER,
    ATTR_RUST_FILTER,
    BUILD,
    assert_not_vacuous,
    run_filter,
    shared_library_name,
)

sys.path.insert(
    0,
    str(Path(__file__).resolve().parents[2] / "projects" / "P03-interrupt-receive" / "python"),
)

import report  # noqa: E402

LANGUAGES = ("C", "C++", "Python", "Rust")

VERDICTS = ("PASS", "BRIDGE", "LATENCY", "THROUGHPUT", "BOTH")


class CStep(ctypes.Structure):
    """Must match attr_step_t in projects/P03-interrupt-receive/c/attribute.h."""

    _fields_ = [
        ("rate", ctypes.c_uint32),
        ("sent", ctypes.c_uint32),
        ("accepted", ctypes.c_uint32),
        ("overruns", ctypes.c_uint32),
        ("dropped", ctypes.c_uint32),
    ]


class CRow(ctypes.Structure):
    """Must match attr_row_t, field for field and in order."""

    _fields_ = [
        ("rate", ctypes.c_uint32),
        ("sent", ctypes.c_uint32),
        ("delivered", ctypes.c_uint32),
        ("overrun", ctypes.c_uint32),
        ("dropped", ctypes.c_uint32),
        ("bridge_lost", ctypes.c_uint32),
        ("target_lost", ctypes.c_uint32),
        ("reached", ctypes.c_uint32),
        ("verdict", ctypes.c_int),
    ]


_LIB = None


def c_lib():
    """The C, which is the reference, so a test without it is skipped and not failed.

    This differs from `payload.dll` and `frame.dll`, which `test_vectors.py` and
    `test_frame.py` fail on when missing. Those libraries are long established
    and a laptop running the suite is expected to have built them. `attribute.c`
    is new as of Saturday 3 October 2026 and win11 aquamarine cannot build it,
    because compiling there is forbidden. A skip naming the command is the
    honest result: the comparison did not run, and saying so beats both a
    failure that points at the code and a pass that compared nothing.
    """
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("attribute")
        if not path.exists():
            pytest.skip(
                "{} is missing, and this laptop does not compile. In WSL on "
                "bing@JPTOUPM678:\n    python3 python/tools/build_host.py".format(path.name)
            )
        d = ctypes.CDLL(str(path))
        d.attr_attribute.restype = ctypes.c_int
        d.attr_attribute.argtypes = [ctypes.POINTER(CStep), ctypes.POINTER(CRow)]
        d.attr_first_loss.restype = ctypes.c_int
        d.attr_first_loss.argtypes = [ctypes.POINTER(CRow), ctypes.c_uint32]
        d.attr_verdict_name.restype = ctypes.c_char_p
        d.attr_verdict_name.argtypes = [ctypes.c_int]
        _LIB = d
    return _LIB


def steps():
    """Every step, with the reason it is here. (rate, sent, accepted, overruns, dropped)."""
    return [
        ("a clean step", (115200, 1_000_000, 1_000_000, 0, 0)),
        ("overrun alone is latency", (460800, 1_000_000, 999_000, 1_000, 0)),
        ("drops alone are throughput", (460800, 1_000_000, 999_000, 0, 1_000)),
        ("both kinds, not summed", (921600, 1_000_000, 998_000, 1_000, 1_000)),
        ("the bridge case", (921600, 1_000_000, 900_000, 0, 0)),
        # The case the verdict ORDER protects, and the one this list was missing
        # until a mutation went uncaught on Saturday 3 October 2026. Both the
        # bridge and the target lost bytes. Testing BRIDGE first makes this
        # BRIDGE, because a run that lost bytes before the peripheral says
        # nothing about the target. Testing the target first would make it
        # LATENCY and publish the target's limit from a run that never measured
        # it, which is the single wrong conclusion this whole file exists to
        # prevent.
        ("the bridge and the target both lost bytes",
         (921600, 1_000_000, 800_000, 1_000, 0)),
        ("the bridge lost bytes and the target lost both kinds",
         (921600, 1_000_000, 700_000, 1_000, 2_000)),
        ("a handful astray in millions", (115200, 10_000_000, 10_000_000 - 3, 0, 0)),
        ("exactly at the tolerance", (115200, 1_000_000, 1_000_000 - 1, 0, 0)),
        ("one byte past the tolerance", (115200, 2_000_000, 2_000_000 - 3, 0, 0)),
        ("a short run, tolerance clamped to one", (9600, 100, 99, 0, 0)),
        ("a short run, two astray", (9600, 100, 98, 0, 0)),
        ("nothing sent at all", (9600, 0, 0, 0, 0)),
        ("everything overran", (921600, 1_000, 0, 1_000, 0)),
        ("everything dropped", (921600, 1_000, 0, 0, 1_000)),
        ("over-accounted, refused", (115200, 1_000, 1_200, 0, 0)),
        ("over-accounted by one", (115200, 1_000, 1_000, 1, 0)),
        ("the largest counters that still fit", (921600, 4_000_000_000, 4_000_000_000, 0, 0)),
        ("three counters that exceed a 32-bit sum", (1, 4_294_967_295, 4_294_967_295,
                                                     4_294_967_295, 4_294_967_295)),
    ]


def ramps():
    """Ramps for first_loss, each with the trap it is checking."""
    return [
        (
            "the bridge gives up above the real finding",
            [
                (115200, 1_000_000, 1_000_000, 0, 0),
                (230400, 1_000_000, 1_000_000, 0, 0),
                (460800, 1_000_000, 999_500, 500, 0),
                (921600, 1_000_000, 500_000, 0, 0),
            ],
        ),
        (
            "every row measured the bridge, so there is no target finding",
            [
                (115200, 1_000_000, 800_000, 0, 0),
                (230400, 1_000_000, 700_000, 0, 0),
            ],
        ),
        (
            "a clean ramp invents no limit",
            [(r, 1_000_000, 1_000_000, 0, 0) for r in (115200, 230400, 460800, 921600)],
        ),
        (
            "unsorted, so the lowest rate is found and not the first row",
            [
                (921600, 1_000_000, 999_000, 1_000, 0),
                (230400, 1_000_000, 999_000, 1_000, 0),
            ],
        ),
    ]


FIELDS = ("rate", "sent", "delivered", "overrun", "dropped",
          "bridge_lost", "target_lost", "reached", "verdict")


def fmt(row):
    """One answer, in the filters' wording, so all four are compared as text."""
    return " ".join("{}={}".format(k, row[k]) for k in FIELDS)


def via_c(cases):
    d = c_lib()
    out = []
    for _, (rate, sent, accepted, overruns, dropped) in cases:
        step = CStep(rate, sent, accepted, overruns, dropped)
        row = CRow()
        rc = d.attr_attribute(ctypes.byref(step), ctypes.byref(row))
        if rc != 0:
            out.append("REFUSED overaccounted")
            continue
        out.append(fmt({
            "rate": row.rate, "sent": row.sent, "delivered": row.delivered,
            "overrun": row.overrun, "dropped": row.dropped,
            "bridge_lost": row.bridge_lost, "target_lost": row.target_lost,
            "reached": row.reached,
            "verdict": d.attr_verdict_name(row.verdict).decode(),
        }))
    return out


def via_python(cases):
    out = []
    for _, (rate, sent, accepted, overruns, dropped) in cases:
        step = {"rate": rate, "sent": sent, "accepted": accepted,
                "overruns": overruns, "dropped": dropped}
        try:
            r = report.attribute(step)
        except ValueError:
            out.append("REFUSED overaccounted")
            continue
        out.append(fmt({
            "rate": r["rate"], "sent": r["sent"], "delivered": r["delivered"],
            "overrun": r["overrun"], "dropped": r["dropped"],
            "bridge_lost": r["bridge_lost"], "target_lost": r["target_lost"],
            "reached": r["reached"], "verdict": r["verdict"],
        }))
    return out


def via_filter(path, cases):
    lines = ["R"] + [
        "A {} {} {} {} {}".format(*values) for _, values in cases
    ]
    answers = run_filter(path, lines)
    assert answers[0] == "reset", answers[0]
    return answers[1:]


def available(cases):
    present = {"C": via_c(cases), "Python": via_python(cases)}
    for name, path in (("C++", ATTR_CPP_FILTER), ("Rust", ATTR_RUST_FILTER)):
        if path.exists():
            present[name] = via_filter(path, cases)
    return present


# --------------------------------------------------------------- the tests

def test_every_language_attributes_every_step_the_same_way(capsys):
    cases = steps()
    results = available(cases)
    reference = results["C"]

    for lang, got in sorted(results.items()):
        for i, (want, have) in enumerate(zip(reference, got)):
            assert have == want, "{} ({}): {} said\n  {}\nand C said\n  {}".format(
                i, cases[i][0], lang, have, want
            )

    # Every verdict must actually occur, or the list is weaker than it reads.
    seen = {a.split("verdict=")[1] for a in reference if "verdict=" in a}
    assert seen == set(VERDICTS), "the step list never produces {}".format(
        sorted(set(VERDICTS) - seen)
    )
    assert "REFUSED overaccounted" in reference, "no step exercises the refusal"

    with capsys.disabled():
        print("\n  P03 parity over {} steps, every verdict and the refusal".format(len(cases)))
        for lang in LANGUAGES:
            print("    {:<7} {}".format(
                lang, "agrees" if lang in results else "not built, not compared"))


def test_every_language_finds_the_same_first_loss():
    """Where the confusion would actually be published: a ramp whose bridge gives
    up at a high rate must not read as the target failing there."""
    for name, ramp in ramps():
        cases = [(name, values) for values in ramp]

        d = c_lib()
        rows = (CRow * len(ramp))()
        for i, (rate, sent, accepted, overruns, dropped) in enumerate(ramp):
            step = CStep(rate, sent, accepted, overruns, dropped)
            assert d.attr_attribute(ctypes.byref(step), ctypes.byref(rows[i])) == 0
        c_at = d.attr_first_loss(rows, len(ramp))

        py_rows = [
            report.attribute({"rate": r, "sent": s, "accepted": a,
                              "overruns": o, "dropped": dr})
            for r, s, a, o, dr in ramp
        ]
        py_row = report.first_loss(py_rows)
        py_at = -1 if py_row is None else py_rows.index(py_row)

        assert py_at == c_at, "{}: Python says index {} and C says {}".format(
            name, py_at, c_at
        )

        for lang, path in (("C++", ATTR_CPP_FILTER), ("Rust", ATTR_RUST_FILTER)):
            if not path.exists():
                continue
            lines = ["R"] + ["A {} {} {} {} {}".format(*v) for v in ramp] + ["F"]
            answers = run_filter(path, lines)
            got = answers[-1]
            want = "first_loss=none" if c_at < 0 else "first_loss={}".format(c_at)
            assert got == want, "{}: {} said {!r} and C said {!r}".format(
                name, lang, got, want
            )


def test_the_float_and_integer_tolerances_never_disagree():
    """The Python computes `int(sent * 1e-6)`; the other three compute
    `sent / 1000000` in integers. They were swept against each other rather than
    assumed equal, and this is that sweep kept as a check.

    `1e-6` is slightly below one millionth, so the product is slightly below the
    true quotient, and the worry was that `int()` would truncate one lower at an
    exact multiple. It never does, because the multiply rounds back onto the
    integer: the relative error is about 2e-17 while the spacing of doubles near
    the largest possible quotient, about 4295, is about 1e-12.
    """
    def float_form(sent):
        return max(1, int(sent * 1e-6))

    def integer_form(sent):
        return max(1, sent // 1_000_000)

    # Every exact multiple and its neighbours across the whole 32-bit range,
    # which is where a truncation would show, plus the small values where the
    # clamp to one is doing the work.
    for sent in range(0, 3000):
        assert float_form(sent) == integer_form(sent), sent
    for k in range(1, 4296):
        for sent in (k * 1_000_000 - 1, k * 1_000_000, k * 1_000_000 + 1):
            if 0 <= sent <= 0xFFFFFFFF:
                assert float_form(sent) == integer_form(sent), sent


def test_the_step_list_exercises_every_verdict_and_the_refusal():
    """Checked in Python, so it is checked on every laptop and not only where a
    compiler exists.

    A parity list that never produces a BRIDGE row would compare four
    implementations of the easy half and prove nothing about the hard one. This
    asserts the list's own adequacy, independently of whether the other three
    implementations are present, and it is the one test in this file that runs on
    win11 aquamarine.
    """
    seen = set()
    refusals = 0
    for name, (rate, sent, accepted, overruns, dropped) in steps():
        step = {"rate": rate, "sent": sent, "accepted": accepted,
                "overruns": overruns, "dropped": dropped}
        try:
            seen.add(report.attribute(step)["verdict"])
        except ValueError:
            refusals += 1

    assert seen == set(VERDICTS), "the list never produces {}".format(
        sorted(set(VERDICTS) - seen)
    )
    assert refusals >= 1, "no step exercises the refusal"

    # The four boundary cases, by name, because they are the ones a careless
    # edit to the tolerance would silently move.
    def verdict(label):
        for name, values in steps():
            if name == label:
                rate, sent, accepted, overruns, dropped = values
                return report.attribute({
                    "rate": rate, "sent": sent, "accepted": accepted,
                    "overruns": overruns, "dropped": dropped,
                })["verdict"]
        raise AssertionError("no step named {!r}".format(label))

    assert verdict("exactly at the tolerance") == "PASS"
    assert verdict("one byte past the tolerance") == "BRIDGE"
    assert verdict("a short run, tolerance clamped to one") == "PASS"
    assert verdict("a short run, two astray") == "BRIDGE"

    # The order of the verdicts, asserted by name rather than trusted to the
    # general comparison. These two steps lost bytes in the bridge AND on the
    # target; BRIDGE is tested first, so BRIDGE is the answer. An
    # implementation that tested the target first would answer LATENCY and BOTH
    # here, and would be publishing a target limit from a run that never
    # measured the target.
    assert verdict("the bridge and the target both lost bytes") == "BRIDGE"
    assert verdict("the bridge lost bytes and the target lost both kinds") == "BRIDGE"


def test_the_comparison_was_not_vacuous():
    assert_not_vacuous(available(steps()[:1]), "P03")


def test_rust_is_compared_when_cargo_has_been_run():
    """Named separately so the CI log says whether Rust was in the comparison."""
    if not ATTR_RUST_FILTER.exists():
        pytest.skip(
            "{} is missing. Build it in WSL on bing@JPTOUPM678 with:\n"
            "    cargo build --release --workspace".format(ATTR_RUST_FILTER.name)
        )
    answers = via_filter(ATTR_RUST_FILTER, [("the bridge case",
                                             (921600, 1_000_000, 900_000, 0, 0))])
    assert "verdict=BRIDGE" in answers[0]
    assert "bridge_lost=100000" in answers[0]
    assert "target_lost=0" in answers[0]
