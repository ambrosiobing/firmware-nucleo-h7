"""P03's attribution, proved on synthetic runs whose answers are known.

The same discipline P06 applies to its witness: an analysis that has only been
shown to accept good data is half tested. The cases below include the ones that
must be REFUSED or attributed elsewhere, because those are the half that matters.

What is being protected. P03's measurement is worthless unless it can tell loss
inside the target from loss in the bridge between the host and the board. A
single "bytes lost" figure would make the most common wrong conclusion easy to
publish: that a link fails above some rate, when the finding is actually about
the bridge. Every case here pins one branch of that attribution.

No hardware, no board, no serial port.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "P03"))

from report import attribute, first_loss  # noqa: E402


def step(rate, sent, accepted, overruns=0, dropped=0):
    return {"rate": rate, "sent": sent, "accepted": accepted,
            "overruns": overruns, "dropped": dropped}


def test_a_clean_step_is_a_pass():
    r = attribute(step(115200, 1_000_000, 1_000_000))
    assert r["verdict"] == "PASS"
    assert r["bridge_lost"] == 0
    assert r["target_lost"] == 0


def test_overrun_alone_is_a_latency_finding():
    """The handler was too late. A larger ring would not help, and saying so is
    the difference between a useful result and a number."""
    r = attribute(step(460800, 1_000_000, 999_000, overruns=1_000))
    assert r["verdict"] == "LATENCY"
    assert r["target_lost"] == 1_000
    assert r["bridge_lost"] == 0


def test_drops_alone_are_a_throughput_finding():
    """The handler ran; the consumer did not keep up. The opposite fix."""
    r = attribute(step(460800, 1_000_000, 999_000, dropped=1_000))
    assert r["verdict"] == "THROUGHPUT"
    assert r["target_lost"] == 1_000


def test_both_kinds_are_not_summed_into_one_verdict():
    r = attribute(step(921600, 1_000_000, 998_000, overruns=1_000, dropped=1_000))
    assert r["verdict"] == "BOTH"
    assert r["overrun"] == 1_000 and r["dropped"] == 1_000


def test_bytes_missing_before_the_peripheral_are_the_bridge_and_not_the_target():
    """The case this whole file exists for.

    The target counted every byte it saw and lost none of them. A tenth of the
    traffic never arrived. That is the bridge, and the target's limit is still
    unknown: reporting it as target loss would be the wrong conclusion.
    """
    r = attribute(step(921600, 1_000_000, 900_000))
    assert r["verdict"] == "BRIDGE"
    assert r["bridge_lost"] == 100_000
    assert r["target_lost"] == 0


def test_a_handful_astray_in_millions_is_not_a_finding():
    """Bridge buffering, not a result. The tolerance is a fraction of the run."""
    r = attribute(step(115200, 10_000_000, 10_000_000 - 3))
    assert r["verdict"] == "PASS"


def test_target_accounting_for_more_than_was_sent_is_refused():
    """A counter that was not reset between steps, or a wrong host count.

    Refusing is correct. Reporting a negative bridge loss, or clamping it to
    zero, would turn a measurement defect into a plausible-looking row.
    """
    with pytest.raises(ValueError, match="not reset|wrong"):
        attribute(step(115200, 1_000, 1_200))


def test_missing_and_negative_counters_are_refused():
    for bad in ({"rate": 1, "sent": 10, "accepted": 10},            # no overruns
                {"rate": 1, "sent": 10, "overruns": 0, "dropped": 0}):  # no accepted
        with pytest.raises(ValueError, match="missing"):
            attribute(bad)
    with pytest.raises(ValueError, match="negative"):
        attribute(step(1, 10, -1))


def test_first_loss_finds_the_lowest_rate_and_ignores_bridge_rows():
    """A ramp where the bridge fails at a high rate must not be read as the
    target failing there. The bridge row is skipped, not counted."""
    rows = [attribute(s) for s in (
        step(115200, 1_000_000, 1_000_000),
        step(230400, 1_000_000, 1_000_000),
        step(460800, 1_000_000, 999_500, overruns=500),   # the real finding
        step(921600, 1_000_000, 500_000),                 # the bridge gave up
    )]
    loss = first_loss(rows)
    assert loss is not None
    assert loss["rate"] == 460800
    assert loss["verdict"] == "LATENCY"


def test_a_ramp_that_only_ever_measured_the_bridge_reports_no_target_finding():
    """Returning None is the honest answer, and the report says so in words."""
    rows = [attribute(s) for s in (
        step(115200, 1_000_000, 800_000),
        step(230400, 1_000_000, 700_000),
    )]
    assert all(r["verdict"] == "BRIDGE" for r in rows)
    assert first_loss(rows) is None


def test_a_clean_ramp_reports_no_loss_rather_than_inventing_a_limit():
    rows = [attribute(step(r, 1_000_000, 1_000_000))
            for r in (115200, 230400, 460800, 921600)]
    assert first_loss(rows) is None
