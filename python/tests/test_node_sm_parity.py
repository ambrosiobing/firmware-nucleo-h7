"""P08's four tables against one another, row by row rather than outcome by outcome.

The oracle here is unlike P09's and P05's, and the difference is the interesting
part. P09 has hand-computed vectors and P05 has a published check value: both are
facts from outside the implementations. P08 has no such external fact, because the
transition table **is** the specification. `../projects/P08-node-state-machine/STATES.md`
was written before any code and says what each state and event means, but it
cannot say what bytes come out.

So the comparison is made stricter instead. Each implementation reports, for every
event in a sequence, **the index of the row it took**, and all four must agree on
every index. That pins the four tables to one order, which matters because two
rows share a from-state and an event: the pair of `TX_FAIL` rows in `S_TX`, told
apart only by their guards. The retry row comes first in all four. If one
implementation put the out-of-retries row first, its retry row would be
unreachable, the node would give up after one attempt instead of three, and a
comparison of final states alone could easily miss it.

Two things anchor the comparison to something outside P08:

  1. **The frame.** P08's transmit stub must hand over exactly what P09's encoder
     produces for the same input. Each implementation calls its own language's
     P09: the C links `payload.c`, the C++ includes `payload.hpp`, the Rust
     depends on the `p09-payload` crate, the Python imports `firmkit.payload`. So
     a frame printed here that matches across four languages is also four
     independent confirmations of P09's layout.
  2. **The retry limit.** Three attempts, not four. The C had this wrong once, by
     counting failures where the guard read attempts, and the sequence below that
     drives three failures is the one that found it.

C is the reference, as in the other two parity tests, because it is the
implementation the board will link.
"""
from __future__ import annotations

import ctypes
import sys
from pathlib import Path

import pytest

from conftest import (
    SM_CPP_FILTER,
    SM_RUST_FILTER,
    assert_not_vacuous,
    run_filter,
)
from test_node_sm import Ctx as CCtx, lib as c_lib

sys.path.insert(
    0,
    str(Path(__file__).resolve().parents[2] / "projects" / "P08-node-state-machine" / "python"),
)

import node_sm as py_sm  # noqa: E402

LANGUAGES = ("C", "C++", "Python", "Rust")

# The event names the four implementations share, in the order node_sm.h
# declares them, so a name maps to the same integer the C uses.
EVENT_NAMES = (
    "TICK", "BLOCK", "FEATURE_DONE", "FRAME_READY",
    "TX_OK", "TX_FAIL", "TIMEOUT", "BUTTON", "FAULT",
)
EVENT_INDEX = {name: i for i, name in enumerate(EVENT_NAMES)}

STATE_NAMES = ("INIT", "IDLE", "SENSE", "FEATURE", "ENCODE", "TX", "BACKOFF", "FAULT")


def sequences():
    """Every sequence, with the sample a caller would have set before BLOCK.

    Chosen to reach all eighteen rows, which the C's own coverage test already
    asserts, plus the cases where the four could plausibly differ: the retry
    ladder, the fault state's lack of an exit, events posted where no row
    applies, and a sequence number driven to its wrap.
    """
    full = ["TICK", "BLOCK", "FEATURE_DONE", "FRAME_READY", "TX_OK"]
    cases = [
        (5, full),
        (0, ["TICK", "TIMEOUT"]),                              # sense: no block in time
        (0, ["FAULT"]),                                        # init: fault before anything
        (0, ["TICK", "FAULT"]),
        (0, ["TICK", "BLOCK", "FAULT"]),
        (0, ["TICK", "BLOCK", "FEATURE_DONE", "FAULT"]),
        (0, ["TICK", "BLOCK", "FEATURE_DONE", "FRAME_READY", "FAULT"]),
        (0, full + ["BUTTON", "BLOCK", "FEATURE_DONE", "FRAME_READY", "TX_OK"]),
        (0, full + ["FAULT"]),
        # The retry ladder: three attempts, then out of retries.
        (7, ["TICK", "BLOCK", "FEATURE_DONE", "FRAME_READY",
             "TX_FAIL", "TIMEOUT", "TX_FAIL", "TIMEOUT", "TX_FAIL"]),
        # One failure then success, which must leave attempts at two.
        (-3, ["TICK", "BLOCK", "FEATURE_DONE", "FRAME_READY", "TX_FAIL", "TIMEOUT", "TX_OK"]),
        # Events that apply to no row, in several states. All four must ignore
        # them identically and count them identically.
        (0, ["TX_OK", "TX_FAIL", "TIMEOUT", "BLOCK", "FEATURE_DONE", "FRAME_READY"]),
        (0, ["TICK", "TICK", "TICK"]),
        (0, ["FAULT"] + list(EVENT_NAMES)),                    # nothing leaves FAULT
        # The signed field at both ends, which is where a sign-extension defect
        # in any one language's P09 would show up through P08's frame.
        (-131072, full),
        (131071, full),
        (-1, full),
        # Several cycles, so the sequence number advances and the frame changes.
        (11, full * 4),
        (0, ["TICK", "TIMEOUT"] * 3),                          # dropped cycles advance it too
    ]
    # Drive the sequence number to its wrap at 512. Cheap: one cycle is five
    # events, so 512 cycles is 2560 events in one line.
    cases.append((1, full * 512))
    return cases


def via_c(cases):
    d = c_lib()
    out = []
    for sample, events in cases:
        ctx = CCtx()
        d.node_sm_init(ctypes.byref(ctx))
        ctx.pending_sample = sample
        rows, handled, unhandled = [], 0, 0
        for name in events:
            row = d.node_sm_dispatch(ctypes.byref(ctx), EVENT_INDEX[name])
            rows.append(row)
            if row < 0:
                unhandled += 1
            else:
                handled += 1
        out.append(
            {
                "state": STATE_NAMES[ctx.state],
                "rows": rows,
                "attempts": ctx.tx_attempts,
                "ok": ctx.cycles_ok,
                "dropped": ctx.cycles_dropped,
                "seq": ctx.sequence,
                "feature": ctx.feature,
                "frame": bytes(ctx.frame[: ctx.frame_len]).hex().upper(),
                "unhandled": unhandled,
                "handled": handled,
            }
        )
    return out


def via_python(cases):
    out = []
    for sample, events in cases:
        ctx = py_sm.init()
        ctx.pending_sample = sample
        rows, handled, unhandled = [], 0, 0
        for name in events:
            row = py_sm.dispatch(ctx, name)
            rows.append(row)
            if row < 0:
                unhandled += 1
            else:
                handled += 1
        out.append(
            {
                "state": ctx.state,
                "rows": rows,
                "attempts": ctx.tx_attempts,
                "ok": ctx.cycles_ok,
                "dropped": ctx.cycles_dropped,
                "seq": ctx.sequence,
                "feature": ctx.feature,
                "frame": ctx.frame.hex().upper(),
                "unhandled": unhandled,
                "handled": handled,
            }
        )
    return out


def via_filter(path, cases):
    """The C++ and the Rust, through the protocol both speak."""
    lines = ["{} {}".format(sample, " ".join(events)) for sample, events in cases]
    out = []
    for answer in run_filter(path, lines):
        fields = dict(part.split("=", 1) for part in answer.split(" "))
        out.append(
            {
                "state": fields["state"],
                "rows": [] if fields["rows"] == "-" else [int(r) for r in fields["rows"].split(",")],
                "attempts": int(fields["attempts"]),
                "ok": int(fields["ok"]),
                "dropped": int(fields["dropped"]),
                "seq": int(fields["seq"]),
                "feature": int(fields["feature"]),
                "frame": "" if fields["frame"] == "-" else fields["frame"],
                "unhandled": int(fields["unhandled"]),
                "handled": int(fields["handled"]),
            }
        )
    return out


def available(cases):
    present = {"C": via_c(cases), "Python": via_python(cases)}
    for name, path in (("C++", SM_CPP_FILTER), ("Rust", SM_RUST_FILTER)):
        if path.exists():
            present[name] = via_filter(path, cases)
    return present


# --------------------------------------------------------------- the tests

def test_every_language_takes_the_same_rows(capsys):
    """The strict comparison: not just the outcome, the path."""
    cases = sequences()
    results = available(cases)
    reference = results["C"]

    for name, got in sorted(results.items()):
        for i, (want, have) in enumerate(zip(reference, got)):
            assert have["rows"] == want["rows"], (
                "case {} ({} events): {} took rows {} and C took {}".format(
                    i, len(cases[i][1]), name, have["rows"], want["rows"]
                )
            )

    with capsys.disabled():
        total = sum(len(events) for _, events in cases)
        print("\n  P08 parity over {} sequences, {} events".format(len(cases), total))
        for name in LANGUAGES:
            print("    {:<7} {}".format(
                name, "agrees" if name in results else "not built, not compared"))


def test_every_language_ends_in_the_same_state_with_the_same_counters():
    cases = sequences()
    results = available(cases)
    reference = results["C"]

    for name, got in sorted(results.items()):
        for i, (want, have) in enumerate(zip(reference, got)):
            for key in ("state", "attempts", "ok", "dropped", "seq", "feature",
                        "unhandled", "handled"):
                assert have[key] == want[key], (
                    "case {}: {} reports {}={} and C reports {}".format(
                        i, name, key, have[key], want[key]
                    )
                )


def test_every_language_hands_the_stub_the_same_frame():
    """Which is also four independent confirmations of P09's bit layout, since
    each implementation calls its own language's encoder rather than a copy."""
    cases = sequences()
    results = available(cases)
    reference = results["C"]

    compared = 0
    for name, got in sorted(results.items()):
        for i, (want, have) in enumerate(zip(reference, got)):
            assert have["frame"] == want["frame"], (
                "case {}: {} framed {} and C framed {}".format(
                    i, name, have["frame"] or "nothing", want["frame"] or "nothing"
                )
            )
            if want["frame"]:
                compared += 1
    assert compared > 0, "no case produced a frame, so the criterion was not tested"


def test_the_retry_limit_is_three_attempts_in_every_language():
    """The off-by-one the C had once, asserted by name rather than left to the
    general comparison, because it is the one defect this project has actually
    had: the counter meant failures while the guard read attempts."""
    case = (7, ["TICK", "BLOCK", "FEATURE_DONE", "FRAME_READY",
                "TX_FAIL", "TIMEOUT", "TX_FAIL", "TIMEOUT", "TX_FAIL"])
    for name, got in sorted(available([case]).items()):
        row = got[0]
        assert row["state"] == "IDLE", "{} ended in {}".format(name, row["state"])
        assert row["attempts"] == 3, "{} made {} attempts".format(name, row["attempts"])
        assert row["dropped"] == 1, "{} dropped {} cycles".format(name, row["dropped"])
        assert row["ok"] == 0, "{} counted a success that did not happen".format(name)


def test_nothing_leaves_the_fault_state_in_any_language():
    case = (0, ["FAULT"] + list(EVENT_NAMES))
    for name, got in sorted(available([case]).items()):
        row = got[0]
        assert row["state"] == "FAULT", "{} left FAULT and is in {}".format(name, row["state"])
        assert row["handled"] == 1, "{} handled {} events, so FAULT has an exit".format(
            name, row["handled"])
        assert row["unhandled"] == len(EVENT_NAMES)


def test_the_comparison_was_not_vacuous():
    assert_not_vacuous(available([(0, ["TICK"])]), "P08")


def test_rust_is_compared_when_cargo_has_been_run():
    """Named separately so the CI log says whether Rust was in the comparison."""
    if not SM_RUST_FILTER.exists():
        pytest.skip(
            "{} is missing. Build it in WSL on bing@JPTOUPM678 with:\n"
            "    cargo build --release --workspace".format(SM_RUST_FILTER.name)
        )
    got = via_filter(SM_RUST_FILTER, [(5, ["TICK", "BLOCK", "FEATURE_DONE", "FRAME_READY", "TX_OK"])])
    assert got[0]["rows"] == [0, 5, 8, 10, 12]
    assert got[0]["state"] == "IDLE"
