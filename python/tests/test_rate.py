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
from pathlib import Path

# The analysis lives in firmkit/. Under pytest that is already on the path, put
# there by tests/conftest.py, but the usage line above promises this file also
# runs as a plain script, and as a script nothing has added it. Both entry
# points have to work or the promise is decoration.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "firmkit"))

from rate import PASS, analyse  # noqa: E402  (after the path is arranged)

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


# ---------------------------------------------------------------- the claims
# One table, two readers. run() prints the human report and test_scenario
# asserts the same claims, so an expectation cannot be changed in one place and
# forgotten in the other. Each claim is a label, a predicate on the analysis
# result, and a detail to print when it does not hold.

def _empty_input():
    """Eight samples that never cross a threshold, so there is no edge at all."""
    return [0.0] * 8


SCENARIOS = [
    ("clean 1 kHz train, must pass", case_clean, [
        ("verdict is pass",
         lambda r: r["pass"] is True,
         lambda r: str(r["checks"])),
        ("rate within 0.5 Hz of 1000",
         lambda r: abs(r["fit_rate_hz"] - 1000.0) < 0.5,
         lambda r: f"{r['fit_rate_hz']:.4f} Hz"),
        ("no missing edges reported",
         lambda r: r["missing_edges"] == 0,
         lambda r: str(r["missing_edges"])),
        ("the two routes agree",
         lambda r: r["routes_agree"] is True,
         lambda r: str(r.get("routes_agree"))),
        ("spread recognised as instrument limited",
         lambda r: "note" in r,
         lambda r: "no note in the result"),
    ]),
    ("1 kHz with 4 us jitter, inside the limit, must pass", case_jittered, [
        ("verdict is pass",
         lambda r: r["pass"] is True,
         lambda r: str(r["checks"])),
        ("rate still within 0.5 Hz",
         lambda r: abs(r["fit_rate_hz"] - 1000.0) < 0.5,
         lambda r: f"{r['fit_rate_hz']:.4f} Hz"),
        ("spread is measured, not zero",
         lambda r: r["interval_sd_s"] > 1e-6,
         lambda r: f"{r['interval_sd_s'] * 1e6:.2f} us"),
        ("spread is under the 10 us limit",
         lambda r: r["interval_sd_s"] <= PASS["jitter_sd_max_s"],
         lambda r: f"{r['interval_sd_s'] * 1e6:.2f} us"),
    ]),
    ("1 kHz with one edge dropped, must be refused", case_dropped, [
        ("verdict is FAIL",
         lambda r: r["pass"] is False,
         lambda r: str(r["checks"])),
        ("the missing edge is counted",
         lambda r: r["missing_edges"] >= 1,
         lambda r: str(r["missing_edges"])),
        ("it fails on the missing edge check",
         lambda r: r["checks"]["no_missing_edges"] is False,
         lambda r: str(r["checks"])),
        ("the mean rate alone would have hidden it",
         lambda r: abs(r["count_rate_hz"] - 1000.0) < 1.0,
         lambda r: f"{r['count_rate_hz']:.4f} Hz"),
    ]),
    ("1002 Hz, twice the tolerance, must be refused", case_wrong, [
        ("verdict is FAIL",
         lambda r: r["pass"] is False,
         lambda r: str(r["checks"])),
        ("it fails on the rate check",
         lambda r: r["checks"]["rate_within_tolerance"] is False,
         lambda r: str(r["checks"])),
        ("the rate it reports is the true one",
         lambda r: abs(r["fit_rate_hz"] - 1002.0) < 0.5,
         lambda r: f"{r['fit_rate_hz']:.4f} Hz"),
        ("it does not blame a missing edge",
         lambda r: r["missing_edges"] == 0,
         lambda r: str(r["missing_edges"])),
    ]),
    ("empty input, must not raise", _empty_input, [
        ("reports an error rather than a number",
         lambda r: "error" in r,
         lambda r: str(sorted(r))),
        ("verdict is FAIL",
         lambda r: r["pass"] is False,
         lambda r: str(r.get("pass"))),
    ]),
]


def run() -> int:
    failures = 0
    for title, build, claims in SCENARIOS:
        print(title)
        result = analyse(build(), FS)
        for label, holds, detail in claims:
            ok = holds(result)
            print(f"  {'pass' if ok else 'FAIL'}  {label}"
                  + (f"   {detail(result)}" if not ok else ""))
            if not ok:
                failures += 1
        print()

    if failures:
        print(f"{failures} checks FAILED. The analysis is not fit to look at "
              f"real data yet.")
    else:
        print("all checks pass. The analysis recovers the right answer from "
              "good input and refuses both a dropped edge and a wrong rate, "
              "so it is fit to look at real data.")
    return 1 if failures else 0


# ---------------------------------------------------------------- the suite
# Imported lazily so the file still runs as a plain script on a machine with no
# test runner installed, which is what its own usage line promises.
try:
    import pytest
except ImportError:                                   # pragma: no cover
    pytest = None

if pytest is not None:
    @pytest.mark.parametrize(
        "title,build,claims", SCENARIOS,
        ids=[t.split(",")[0].replace(" ", "-") for t, _b, _c in SCENARIOS])
    def test_scenario(title, build, claims):
        """Every claim in one scenario, so a failure names all of them at once."""
        result = analyse(build(), FS)
        broken = [f"{label}: {detail(result)}"
                  for label, holds, detail in claims if not holds(result)]
        assert not broken, title + "\n  " + "\n  ".join(broken)


if __name__ == "__main__":
    sys.exit(run())
