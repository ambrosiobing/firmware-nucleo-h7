#!/usr/bin/env python3
"""The analysis, tested against input whose answer is known by construction.

    python test_rate.py

Runs on any machine with Python and needs no hardware, which is why it is
written and run before the board is ever connected. Four cases, and the
analysis has to get all four right:

  clean      a perfect 1 kHz train                     -> pass
  jittered   1 kHz with jitter inside the limit        -> pass
  dropped    1 kHz with one edge missing               -> FAIL, and say why
  wrong      1002 Hz, outside the rate tolerance       -> FAIL, and say why

A test that only proves the analysis accepts good input is half a test. Two of
these four prove it rejects bad input, which is the half that matters, because
an analysis that passes everything would have passed the board too.
"""
from __future__ import annotations

import math
import random
import sys

from rate import PASS, analyse

FS = 100_000.0          # the witness rate, 100 kS/s on one channel
HIGH, LOW = 3.3, 0.0    # volts, as the marker pin actually swings


def square(edge_times: list[float], duration_s: float, fs: float,
           duty: float = 0.5, period_s: float = 1e-3,
           noise_v: float = 0.0, seed: int = 1) -> list[float]:
    """Sample a square wave whose rising edges are exactly edge_times."""
    rnd = random.Random(seed)
    n = int(duration_s * fs)
    out = [LOW] * n
    high_for = duty * period_s
    for t0 in edge_times:
        a = int(round(t0 * fs))
        b = int(round((t0 + high_for) * fs))
        for i in range(max(a, 0), min(b, n)):
            out[i] = HIGH
    if noise_v:
        out = [v + rnd.gauss(0.0, noise_v) for v in out]
    return out


def case_clean(duration=2.0, rate=1000.0):
    p = 1.0 / rate
    edges = [0.002 + k * p for k in range(int((duration - 0.004) / p))]
    return square(edges, duration, FS, period_s=p, noise_v=0.005, seed=11)


def case_jittered(duration=2.0, rate=1000.0, sd_s=4e-6):
    """Jitter well inside the 10 microsecond limit, so it must still pass."""
    rnd = random.Random(22)
    p = 1.0 / rate
    edges, t = [], 0.002
    for _ in range(int((duration - 0.004) / p)):
        edges.append(t + rnd.gauss(0.0, sd_s))
        t += p
    return square(edges, duration, FS, period_s=p, noise_v=0.005, seed=23)


def case_dropped(duration=2.0, rate=1000.0, drop_at=500):
    p = 1.0 / rate
    edges = [0.002 + k * p for k in range(int((duration - 0.004) / p))]
    del edges[drop_at]
    return square(edges, duration, FS, period_s=p, noise_v=0.005, seed=33)


def case_wrong(duration=2.0, rate=1002.0):
    """0.2 percent fast, twice the tolerance. Must be refused."""
    p = 1.0 / rate
    edges = [0.002 + k * p for k in range(int((duration - 0.004) / p))]
    return square(edges, duration, FS, period_s=p, noise_v=0.005, seed=44)


def run() -> int:
    failures = 0

    def check(label: str, cond: bool, detail: str = "") -> None:
        nonlocal failures
        print(f"  {'pass' if cond else 'FAIL'}  {label}"
              + (f"   {detail}" if detail and not cond else ""))
        if not cond:
            failures += 1

    print("clean 1 kHz train, must pass")
    r = analyse(case_clean(), FS)
    check("verdict is pass", r["pass"] is True, str(r["checks"]))
    check("rate within 0.5 Hz of 1000",
          abs(r["fit_rate_hz"] - 1000.0) < 0.5, f"{r['fit_rate_hz']:.4f} Hz")
    check("no missing edges reported", r["missing_edges"] == 0,
          str(r["missing_edges"]))
    check("the two routes agree", r["routes_agree"] is True)
    check("spread recognised as instrument limited", "note" in r)
    print()

    print("1 kHz with 4 us jitter, inside the limit, must pass")
    r = analyse(case_jittered(), FS)
    check("verdict is pass", r["pass"] is True, str(r["checks"]))
    check("rate still within 0.5 Hz",
          abs(r["fit_rate_hz"] - 1000.0) < 0.5, f"{r['fit_rate_hz']:.4f} Hz")
    check("spread is measured, not zero", r["interval_sd_s"] > 1e-6,
          f"{r['interval_sd_s'] * 1e6:.2f} us")
    check("spread is under the 10 us limit",
          r["interval_sd_s"] <= PASS["jitter_sd_max_s"],
          f"{r['interval_sd_s'] * 1e6:.2f} us")
    print()

    print("1 kHz with one edge dropped, must be refused")
    r = analyse(case_dropped(), FS)
    check("verdict is FAIL", r["pass"] is False)
    check("the missing edge is counted", r["missing_edges"] >= 1,
          str(r["missing_edges"]))
    check("it fails on the missing edge check",
          r["checks"]["no_missing_edges"] is False)
    check("the mean rate alone would have hidden it",
          abs(r["count_rate_hz"] - 1000.0) < 1.0,
          f"{r['count_rate_hz']:.4f} Hz")
    print()

    print("1002 Hz, twice the tolerance, must be refused")
    r = analyse(case_wrong(), FS)
    check("verdict is FAIL", r["pass"] is False)
    check("it fails on the rate check",
          r["checks"]["rate_within_tolerance"] is False)
    check("the rate it reports is the true one",
          abs(r["fit_rate_hz"] - 1002.0) < 0.5, f"{r['fit_rate_hz']:.4f} Hz")
    check("it does not blame a missing edge", r["missing_edges"] == 0,
          str(r["missing_edges"]))
    print()

    print("empty input, must not raise")
    r = analyse([0.0] * 8, FS)
    check("reports an error rather than a number", "error" in r)
    check("verdict is FAIL", r["pass"] is False)
    print()

    if failures:
        print(f"{failures} checks FAILED. The analysis is not fit to look at "
              f"real data yet.")
    else:
        print("all checks pass. The analysis recovers the right answer from "
              "good input and refuses both a dropped edge and a wrong rate, "
              "so it is fit to look at real data.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(run())
