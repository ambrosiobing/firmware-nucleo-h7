#!/usr/bin/env python3
"""Capture the marker pin with the MCC 118 HAT, on the Raspberry Pi 4.

    python3 scan.py --seconds 60 --build timer --out run-001

Writes two files beside each other:
    run-001.csv   one voltage per line, nothing else
    run-001.json  the metadata, without which the capture is not evidence

Runs on the Pi, not on the authoring laptop: it needs the daqhats library and
the HAT on the 40-pin header. Nothing here analyses anything. Capture and
analysis are separate so a capture can be re-analysed after the analysis is
improved, which is the whole reason the raw samples are kept.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

CHANNEL = 0           # the marker wire goes to CH0 on the screw terminal block
DEFAULT_RATE = 100_000.0   # one channel, so the full aggregate rate is available


def git_describe(where: Path) -> str:
    """The build identity. A capture with no build identity is not evidence."""
    try:
        out = subprocess.run(["git", "describe", "--always", "--dirty", "--tags"],
                             cwd=where, capture_output=True, text=True, timeout=10)
        if out.returncode == 0:
            return out.stdout.strip()
        return "not-a-git-checkout"
    except (OSError, subprocess.SubprocessError):
        return "git-unavailable"


def capture(seconds: float, rate: float, channel: int) -> tuple[list[float], dict]:
    """Read one channel continuously. Returns the samples and what the HAT said."""
    try:
        from daqhats import mcc118, hat_list, HatIDs, OptionFlags
        from daqhats import SourceType, TriggerModes  # noqa: F401  (documented below)
    except ImportError:
        sys.exit("daqhats is not installed. This script runs on the Pi with the "
                 "MCC 118 fitted, not on the authoring laptop.")

    boards = hat_list(filter_by_id=HatIDs.MCC_118)
    if not boards:
        sys.exit("no MCC 118 found on the 40-pin header. Check it is seated and "
                 "that the Pi has been power cycled since it was fitted.")
    board = mcc118(boards[0].address)

    info = {
        "hat_address": boards[0].address,
        "hat_product": boards[0].product_name,
        "hat_serial": board.serial(),
        "channel": channel,
        "requested_rate_hz": rate,
        # The input range and whether an external trigger exists are open
        # questions against the HAT user guide, recorded rather than assumed.
        "input_range_v": "read from the user guide, not assumed",
        "external_trigger_used": False,
    }

    n = int(seconds * rate)
    mask = 1 << channel
    board.a_in_scan_start(mask, n, rate, OptionFlags.CONTINUOUS)
    actual = board.a_in_scan_actual_rate(1, rate)
    info["actual_rate_hz"] = actual

    samples: list[float] = []
    t_end = time.monotonic() + seconds
    overrun = False
    try:
        while time.monotonic() < t_end and len(samples) < n:
            r = board.a_in_scan_read(-1, 0.5)
            if r.hardware_overrun or r.buffer_overrun:
                overrun = True
                break
            samples.extend(r.data)
    finally:
        board.a_in_scan_stop()
        board.a_in_scan_cleanup()

    info["overrun"] = overrun
    info["samples"] = len(samples)
    return samples, info


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--seconds", type=float, default=60.0,
                    help="60 is the run length the method document specifies")
    ap.add_argument("--rate", type=float, default=DEFAULT_RATE)
    ap.add_argument("--channel", type=int, default=CHANNEL)
    ap.add_argument("--build", required=True,
                    choices=["systick", "timer", "dma"],
                    help="which firmware build is on the board right now")
    ap.add_argument("--out", required=True, help="basename for the two files")
    ap.add_argument("--note", default="", help="anything unusual about this run")
    a = ap.parse_args(argv)

    # A capture is evidence, so an existing output is refused rather than
    # replaced. On Friday 9 October 2026 a second 60 second run reused --out and
    # overwrote the first, which held the only interval more than 50 us off
    # nominal seen that day; it survives only in a pasted report. Refusing here
    # costs a new name and saves the thing the whole chain exists to produce.
    out = Path(a.out)
    taken = [q for q in (out.with_suffix(".csv"), out.with_suffix(".json")) if q.exists()]
    if taken:
        sys.exit("refusing to overwrite {}: a capture is evidence, choose a new --out"
                 .format(", ".join(str(q) for q in taken)))

    samples, info = capture(a.seconds, a.rate, a.channel)

    meta = {
        "captured_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "firmware_build": a.build,
        "firmware_identity": git_describe(Path(__file__).resolve().parent.parent),
        "witness": "MCC 118 on Raspberry Pi 4",
        "note": a.note,
        **info,
    }

    out.with_suffix(".csv").write_text("".join(f"{v:.6f}\n" for v in samples))
    out.with_suffix(".json").write_text(json.dumps(meta, indent=2) + "\n")

    print(f"{len(samples)} samples at {info['actual_rate_hz']:.1f} Hz "
          f"-> {out.with_suffix('.csv').name}")
    if info["overrun"]:
        print("OVERRUN reported by the HAT. This capture has a gap in it and "
              "must not be analysed. Lower the rate or shorten the run.")
        return 1
    rate_py = Path(__file__).resolve().parents[3] / "python" / "firmkit" / "rate.py"
    print(f"analyse it with:  python3 {rate_py} {out.with_suffix('.csv').resolve()} "
          f"--fs {info['actual_rate_hz']:.6f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
