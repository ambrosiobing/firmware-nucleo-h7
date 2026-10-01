#!/usr/bin/env python3
"""The rate ramp: send a known pattern faster and faster, and record what arrived.

    python python/P03/loadgen.py COM7 --out run.json

NOT YET RUN. pyserial is not installed on win11 aquamarine as of Thursday
1 October 2026 and there is no firmware on the board, so this is written against
the measurement method and left to be proven when both exist. What has been
proven is the half that needs neither: python/P03/report.py attributes every byte
and python/tests/test_report.py holds it to that on synthetic runs whose answers
are known by construction.

That order is deliberate and it is P06's lesson. A method written after the first
plot is a rationalisation of whatever the plot showed, and an analysis first shown
to accept good data is half tested. So the attribution was written and tested
before any byte crossed a wire.

WHAT THE RAMP MUST DO TO MEAN ANYTHING.

Reset the target's counters at the start of every step, or the attribution in
report.py refuses the run, which is the behaviour you want: a counter carried
between steps makes every row after the first wrong in a way no reader could
spot.

Send a pattern whose every byte is checkable, not random bytes. A counting
sequence lets the target say not only how many bytes it took but whether they were
the right ones in the right order, which separates loss from corruption. Those are
different findings and a rate ramp that cannot tell them apart will blame the
rate for a cable.

Record the firmware's own build identity in the run file. A capture with no build
identity is not evidence, and report.py says so when it is missing.

Hold each rate long enough that the finding is not a transient. The bridge
buffers, so a short burst at a rate the link cannot sustain is absorbed and looks
like a pass.

WHAT THIS CANNOT SETTLE, and the chapter has to say so. The host end of the link
is a USB bridge inside the debug probe, and its own limits are not documented
anywhere this project can cite. When a step reports BRIDGE, the honest statement
is that the bridge did not carry the traffic, not that the board could not. The
target's real limit is only established at rates where bridge_lost is zero.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    serial = None

# The default ramp. Each is a rate the bridge is likely to offer; whether it
# sustains them is the thing being measured rather than assumed.
DEFAULT_RATES = [115200, 230400, 460800, 921600, 1000000, 2000000]
DEFAULT_SECONDS = 10.0


def build_identity() -> str:
    """git describe, so a run can be tied to the firmware that produced it."""
    try:
        out = subprocess.run(
            ["git", "describe", "--always", "--dirty"],
            capture_output=True, text=True, timeout=10,
            cwd=Path(__file__).resolve().parent.parent.parent)
        return out.stdout.strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def pattern(n: int, start: int = 0) -> bytes:
    """A counting sequence, so the target can check order as well as count.

    Random bytes would measure loss and nothing else. With a counting sequence a
    target that receives the right number of wrong bytes reports corruption
    rather than a clean pass, and corruption at a high rate is a cable or a
    divider, not a capacity limit.
    """
    return bytes((start + i) & 0xFF for i in range(n))


def run_step(port, rate: int, seconds: float) -> dict:
    """One rate, held for `seconds`. Returns the host's half of the accounting."""
    raise NotImplementedError(
        "The target protocol is P03's and is not written yet: resetting the\n"
        "counters and reading them back needs a command the firmware does not\n"
        "have, and inventing one here would be a protocol nobody has reviewed.\n"
        "What exists and is tested is the attribution in report.py."
    )


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("port", help="COM7 on win11 aquamarine, /dev/ttyACM0 on Linux")
    ap.add_argument("--out", type=Path, default=Path("run.json"))
    ap.add_argument("--rates", type=int, nargs="+", default=DEFAULT_RATES)
    ap.add_argument("--seconds", type=float, default=DEFAULT_SECONDS)
    args = ap.parse_args(argv[1:])

    if serial is None:
        print("pyserial is not installed. It is in requirements-hardware.txt,")
        print("which the test suite and the CI runner deliberately never install:")
        print("    python -m pip install -r requirements-hardware.txt")
        return 1

    steps = []
    for rate in args.rates:
        print("  {} baud for {} s".format(rate, args.seconds))
        with serial.Serial(args.port, rate, timeout=1) as port:
            time.sleep(0.2)                      # let the bridge settle
            steps.append(run_step(port, rate, args.seconds))

    run = {"path": "unknown", "build": build_identity(), "fault": "none",
           "steps": steps}
    args.out.write_text(json.dumps(run, indent=2), encoding="utf-8")
    print("wrote {}. Now: python python/P03/report.py {}".format(args.out, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
