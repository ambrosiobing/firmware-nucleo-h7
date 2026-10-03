#!/usr/bin/env python3
"""Turn a rate-ramp run into a results table, and attribute every lost byte.

    python projects/P03-interrupt-receive/python/report.py run.json

THE WHOLE POINT OF THIS FILE. The easiest way to publish a wrong conclusion about
a serial link is to report one figure called "bytes lost". The common finding,
that a link fails above some rate, is usually a statement about the bridge
between the host and the board rather than about the board. A number that cannot
tell those apart is worse than no number, because somebody will quote it.

So every byte the host sent is attributed to exactly one place:

    delivered     the target handed it to the application
    overrun       the peripheral had it and the handler was too late to read it
    dropped       the handler read it and the ring was full
    bridge_lost   it never reached the peripheral at all

The first three come from counters on the target. The fourth is a subtraction and
it is the one that decides whether the run means anything: if bytes went missing
before the peripheral saw them, the run measured the bridge and the target's
limit is still unknown.

The two kinds of target loss are also different findings and are never summed.
An overrun is a latency failure: the handler did not run in time. A drop is a
throughput failure: the handler ran, but the consumer did not keep up. One is
fixed by shortening the handler or raising its priority, the other by a larger
ring or a faster consumer, and a single "lost" figure points at neither.

No hardware is needed to run this. It reads a JSON file that
projects/P03-interrupt-receive/python/loadgen.py writes, and python/tests/test_report.py proves the
attribution on synthetic runs whose answers are known by construction, which is
the same discipline P06 applies to its witness: an analysis that has only been
shown to accept good data is half tested.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

# A handful of bytes astray in a run of millions is the bridge's buffering rather
# than a finding. Above this fraction the run is about the bridge and says
# nothing about the target.
BRIDGE_TOLERANCE_FRACTION = 1e-6


def attribute(step: dict) -> dict:
    """Attribute every sent byte to exactly one place.

    `step` carries what the host sent and what the target counted:
        sent, accepted, overruns, dropped, framing, noise
    """
    for key in ("sent", "accepted", "overruns", "dropped"):
        if key not in step:
            raise ValueError("step is missing {!r}".format(key))
        if step[key] < 0:
            raise ValueError("{} is negative: {}".format(key, step[key]))

    sent = step["sent"]
    delivered = step["accepted"]
    overrun = step["overruns"]
    dropped = step["dropped"]

    reached = delivered + overrun + dropped
    bridge_lost = sent - reached

    # A target that accounts for more bytes than the host sent is not a finding
    # about the link, it is a defect in the measurement: a counter that was not
    # reset between steps, or a host count that is wrong. Refusing is correct.
    if bridge_lost < 0:
        raise ValueError(
            "the target accounted for {} bytes and the host sent {}. "
            "A counter was not reset, or the host count is wrong.".format(reached, sent)
        )

    target_lost = overrun + dropped
    tolerance = max(1, int(sent * BRIDGE_TOLERANCE_FRACTION))

    if bridge_lost > tolerance:
        verdict = "BRIDGE"      # says nothing about the target
    elif target_lost == 0:
        verdict = "PASS"
    elif overrun > 0 and dropped == 0:
        verdict = "LATENCY"     # the handler was too late
    elif dropped > 0 and overrun == 0:
        verdict = "THROUGHPUT"  # the consumer could not keep up
    else:
        verdict = "BOTH"

    return {
        "rate": step.get("rate"),
        "sent": sent,
        "delivered": delivered,
        "overrun": overrun,
        "dropped": dropped,
        "bridge_lost": bridge_lost,
        "target_lost": target_lost,
        "reached": reached,
        "verdict": verdict,
    }


def first_loss(rows: list[dict]) -> dict | None:
    """The lowest rate at which the target itself lost a byte.

    Rows with a BRIDGE verdict are skipped rather than counted as a target
    failure, because that is exactly the confusion this whole file exists to
    prevent. A ramp whose every row is BRIDGE has found nothing about the target
    and first_loss says so by returning None.
    """
    for row in sorted(rows, key=lambda r: r["rate"] or 0):
        if row["verdict"] in ("LATENCY", "THROUGHPUT", "BOTH"):
            return row
    return None


def table(rows: list[dict]) -> str:
    out = [
        "  {:>8}  {:>10}  {:>10}  {:>8}  {:>8}  {:>11}  {}".format(
            "rate", "sent", "delivered", "overrun", "dropped", "bridge lost", "verdict"),
        "  {:>8}  {:>10}  {:>10}  {:>8}  {:>8}  {:>11}  {}".format(
            "-" * 8, "-" * 10, "-" * 10, "-" * 8, "-" * 8, "-" * 11, "-" * 10),
    ]
    for r in sorted(rows, key=lambda x: x["rate"] or 0):
        out.append("  {:>8}  {:>10}  {:>10}  {:>8}  {:>8}  {:>11}  {}".format(
            r["rate"], r["sent"], r["delivered"], r["overrun"], r["dropped"],
            r["bridge_lost"], r["verdict"]))
    return "\n".join(out)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.splitlines()[0])
        print("usage: python projects/P03-interrupt-receive/python/report.py <run.json>")
        return 2

    with open(Path(argv[1]), encoding="utf-8") as fh:
        run = json.load(fh)

    rows = [attribute(s) for s in run["steps"]]
    print("receive path: {}".format(run.get("path", "unknown")))
    print("build:        {}".format(run.get("build", "not recorded")))
    print("fault mode:   {}".format(run.get("fault", "none")))
    print()
    print(table(rows))
    print()

    loss = first_loss(rows)
    if loss is None:
        if all(r["verdict"] == "BRIDGE" for r in rows):
            print("Every step lost bytes before the peripheral saw them. This run")
            print("measured the bridge and says nothing about the target.")
        else:
            print("No target loss at any rate tried. The limit is above this ramp.")
    else:
        print("First target loss at {} baud, by {}: overrun {}, dropped {}.".format(
            loss["rate"], loss["verdict"].lower(), loss["overrun"], loss["dropped"]))
        if loss["verdict"] == "LATENCY":
            print("An overrun is the handler running too late. Shorten it or raise")
            print("its priority; a larger ring will not help.")
        elif loss["verdict"] == "THROUGHPUT":
            print("A drop is the consumer not keeping up. A larger ring or a faster")
            print("consumer will help; shortening the handler will not.")

    # A capture with no build identity is not evidence.
    if not run.get("build"):
        print()
        print("WARNING: this run records no build identity, so it is not evidence.")
        print("loadgen.py records git describe; a run without it cannot be tied")
        print("to the firmware that produced it.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
