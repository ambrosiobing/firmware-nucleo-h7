#!/usr/bin/env python3
"""Recover the sampling rate from a recording of the marker pin.

    python rate.py capture.npy --fs 100000
    python rate.py capture.csv --fs 100000 --json

Reads a single-channel recording of a square wave produced by the firmware's
marker pin, finds the rising edges, and reports the rate two independent ways
that must agree: a count, and a straight-line fit of edge index against edge
time. The fit is the reported rate and the count is the check on it.

The pass criteria live in docs/measurement.md and were written before any
hardware was connected. They are repeated here in one place, PASS, so that the
script and the document cannot drift apart silently.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

# The criteria from docs/measurement.md. Changing a number here is a change to
# the claim, so it is a change to that document too.
PASS = {
    "nominal_hz": 1000.0,
    "rate_tolerance_fraction": 0.001,   # criterion 1: 0.1 percent
    "jitter_sd_max_s": 10e-6,           # criterion 2: 10 microseconds
    "worst_interval_dev_max_s": 50e-6,  # criterion 3: 50 microseconds
    "max_missing_edges": 0,             # criterion 4
}


def load(path: Path) -> list[float]:
    """One channel of samples, from .npy or a single-column text file."""
    if path.suffix == ".npy":
        try:
            import numpy as np
        except ImportError:
            sys.exit("reading .npy needs numpy; use a text capture instead")
        a = np.load(path)
        return [float(x) for x in (a[:, 0] if a.ndim > 1 else a)]
    out = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line[0] not in "-+.0123456789":
            continue            # header or comment
        out.append(float(line.split(",")[0]))
    return out


def rising_edges(samples: list[float], fs: float,
                 low: float | None = None,
                 high: float | None = None) -> list[float]:
    """Edge times in seconds, by linear interpolation across the threshold.

    A Schmitt-style pair of thresholds, set from the observed swing rather than
    assumed to be 0 and 3.3 volts, so a recording through an attenuator or with
    an offset still works. Interpolation is what gets the edge time below one
    sample period, which criterion 2 depends on.
    """
    if len(samples) < 16:
        return []
    lo_obs, hi_obs = min(samples), max(samples)
    swing = hi_obs - lo_obs
    if swing <= 0:
        return []
    low = lo_obs + 0.3 * swing if low is None else low
    high = lo_obs + 0.7 * swing if high is None else high

    edges: list[float] = []
    armed = samples[0] < low          # must go low before a rise counts
    for i in range(1, len(samples)):
        prev, cur = samples[i - 1], samples[i]
        if armed and cur >= high:
            # interpolate the crossing of the midpoint between the thresholds
            mid = 0.5 * (low + high)
            if cur != prev:
                frac = (mid - prev) / (cur - prev)
                frac = min(max(frac, 0.0), 1.0)
            else:
                frac = 0.0
            edges.append((i - 1 + frac) / fs)
            armed = False
        elif cur < low:
            armed = True
    return edges


def fit_line(x: list[float], y: list[float]) -> tuple[float, float, float]:
    """Least squares gradient, intercept, and the standard error of the gradient.

    Written out rather than imported so the witness needs nothing beyond the
    standard library on the Raspberry Pi.
    """
    n = len(x)
    if n < 3:
        return float("nan"), float("nan"), float("nan")
    mx = sum(x) / n
    my = sum(y) / n
    sxx = sum((xi - mx) ** 2 for xi in x)
    if sxx == 0:
        return float("nan"), float("nan"), float("nan")
    sxy = sum((xi - mx) * (yi - my) for xi, yi in zip(x, y))
    slope = sxy / sxx
    intercept = my - slope * mx
    resid = [yi - (slope * xi + intercept) for xi, yi in zip(x, y)]
    if n > 2:
        s2 = sum(r * r for r in resid) / (n - 2)
        se = math.sqrt(s2 / sxx)
    else:
        se = float("nan")
    return slope, intercept, se


def analyse(samples: list[float], fs: float,
            nominal_hz: float = PASS["nominal_hz"]) -> dict:
    """Everything docs/measurement.md asks for, and the verdict on each."""
    edges = rising_edges(samples, fs)
    r: dict = {
        "sample_rate_hz": fs,
        "duration_s": len(samples) / fs if fs else 0.0,
        "edges": len(edges),
        "nominal_hz": nominal_hz,
        "resolution_s": 1.0 / fs if fs else float("nan"),
    }
    if len(edges) < 3:
        r["error"] = "fewer than three edges; nothing to measure"
        r["pass"] = False
        return r

    span = edges[-1] - edges[0]
    r["count_rate_hz"] = (len(edges) - 1) / span if span > 0 else float("nan")

    idx = list(range(len(edges)))
    period, _, period_se = fit_line(idx, edges)
    r["fit_period_s"] = period
    r["fit_rate_hz"] = 1.0 / period if period else float("nan")
    # dRate = dPeriod / period^2
    r["fit_rate_se_hz"] = (period_se / (period * period)) if period else float("nan")

    intervals = [b - a for a, b in zip(edges, edges[1:])]
    mean_iv = sum(intervals) / len(intervals)
    var = sum((v - mean_iv) ** 2 for v in intervals) / (len(intervals) - 1) \
        if len(intervals) > 1 else 0.0
    r["interval_mean_s"] = mean_iv
    r["interval_sd_s"] = math.sqrt(var)
    nominal_iv = 1.0 / nominal_hz
    worst = max(intervals, key=lambda v: abs(v - nominal_iv))
    r["interval_worst_s"] = worst
    r["interval_worst_dev_s"] = abs(worst - nominal_iv)

    # A dropped edge shows as an interval near an integer multiple of nominal.
    # A doubled edge shows as one near a half. Count both as missing edges.
    missing = 0
    for v in intervals:
        k = v / nominal_iv
        nearest = round(k)
        if nearest >= 2 and abs(k - nearest) < 0.25:
            missing += nearest - 1
        elif nearest == 0 or (k < 0.75 and k > 0):
            missing += 1
    r["missing_edges"] = missing

    # The two routes must agree, or the analysis is wrong and not the firmware.
    r["routes_agree"] = (
        abs(r["count_rate_hz"] - r["fit_rate_hz"]) < 0.01 * nominal_hz
        if not math.isnan(r["fit_rate_hz"]) else False)

    checks = {
        "rate_within_tolerance": abs(r["fit_rate_hz"] - nominal_hz)
        <= PASS["rate_tolerance_fraction"] * nominal_hz,
        "jitter_within_limit": r["interval_sd_s"] <= PASS["jitter_sd_max_s"],
        "worst_interval_within_limit": r["interval_worst_dev_s"]
        <= PASS["worst_interval_dev_max_s"],
        "no_missing_edges": r["missing_edges"] <= PASS["max_missing_edges"],
        "routes_agree": r["routes_agree"],
    }
    r["checks"] = checks
    r["pass"] = all(checks.values())

    # The claim can never be finer than the instrument. Say so in the result
    # rather than leaving the reader to work it out.
    if r["interval_sd_s"] < r["resolution_s"] / 2:
        r["note"] = ("measured spread is below half the witness sample period, "
                     "so it is limited by the instrument and not by the board")
    return r


def report(r: dict) -> str:
    if "error" in r:
        return f"FAIL: {r['error']}"
    us = 1e6
    lines = [
        f"duration            {r['duration_s']:.3f} s, {r['edges']} rising edges",
        f"witness resolution  {r['resolution_s'] * us:.1f} us per sample",
        f"rate, fitted        {r['fit_rate_hz']:.4f} +/- {r['fit_rate_se_hz']:.4f} Hz",
        f"rate, counted       {r['count_rate_hz']:.4f} Hz",
        f"interval mean       {r['interval_mean_s'] * us:.2f} us",
        f"interval spread     {r['interval_sd_s'] * us:.2f} us standard deviation",
        f"worst interval      {r['interval_worst_s'] * us:.2f} us,"
        f" {r['interval_worst_dev_s'] * us:.2f} us from nominal",
        f"missing edges       {r['missing_edges']}",
        "",
    ]
    for name, ok in r["checks"].items():
        lines.append(f"  {'pass' if ok else 'FAIL'}  {name}")
    if "note" in r:
        lines += ["", f"note: {r['note']}"]
    lines += ["", "VERDICT: " + ("pass" if r["pass"] else "FAIL")]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("capture", type=Path)
    ap.add_argument("--fs", type=float, required=True,
                    help="witness sample rate in Hz, from the capture metadata")
    ap.add_argument("--nominal", type=float, default=PASS["nominal_hz"])
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)

    samples = load(a.capture)
    r = analyse(samples, a.fs, a.nominal)
    print(json.dumps(r, indent=2) if a.json else report(r))
    return 0 if r.get("pass") else 1


if __name__ == "__main__":
    sys.exit(main())
