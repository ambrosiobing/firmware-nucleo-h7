"""P07's cycle segmentation and battery arithmetic, the twin of c/cyclecharge.c.

Chapter 7's step 9 asks for exactly this: the segmentation tested on the host
against a trace whose answer is known by construction, in continuous integration,
with no instrument attached. Standard library only, so the runner needs nothing.

It also carries, at the bottom, step 8's segmentation and step 10's lifetime
exactly as printed, written without NumPy so they can be run here. Nothing above
calls either, and the tests run both, because all three findings in this project
are about code the chapter prints:

* step 9's own test asserts nine cycles where the printed code returns eight,
* the printed window is off by one sample in a way that test cannot see,
* and the printed lifetime leaves most of the day free of charge.
"""
from __future__ import annotations

CYC_MAX_CYCLES = 64
CYC_MIN_CYCLES = 10
SECONDS_PER_DAY = 86400.0

ERR_ARGUMENT = -1
ERR_RATE = -2
ERR_BIT = -3
ERR_ROOM = -4
ERR_CYCLES = -5
ERR_SUPPLY = -6
ERR_DAY = -7

ERRORS = {"argument": ERR_ARGUMENT, "rate": ERR_RATE, "bit": ERR_BIT,
          "room": ERR_ROOM, "cycles": ERR_CYCLES, "supply": ERR_SUPPLY,
          "day": ERR_DAY}


def per_cycle_charge(current_ua, digital, fs_hz, cycle_bit=0,
                     max_out=CYC_MAX_CYCLES):
    """Charge per complete cycle, in microcoulombs. Returns (rc, list)."""
    if current_ua is None or digital is None or len(current_ua) == 0:
        return ERR_ARGUMENT, None
    if len(current_ua) != len(digital):
        return ERR_ARGUMENT, None
    if not fs_hz > 0.0:
        return ERR_RATE, None
    if cycle_bit > 7:
        return ERR_BIT, None

    out = []
    previous_start = 0
    have_start = False
    for i in range(1, len(digital)):
        was = (digital[i - 1] >> cycle_bit) & 1
        now = (digital[i] >> cycle_bit) & 1
        if was or not now:
            continue
        # i is the first sample INSIDE the cycle, not the last one outside it.
        if have_start:
            if len(out) >= max_out:
                return ERR_ROOM, None
            total = 0.0
            for k in range(previous_start, i):
                total += current_ua[k]
            out.append(total / fs_hz)
        previous_start = i
        have_start = True
    return 0, out


def report(charge_uc, volts=3.3, min_cycles=CYC_MIN_CYCLES):
    """The median, the extremes and the spread, refusing too few cycles."""
    if charge_uc is None:
        return ERR_ARGUMENT, None
    if len(charge_uc) > CYC_MAX_CYCLES:
        return ERR_ROOM, None
    if not volts > 0.0:
        return ERR_SUPPLY, None
    if len(charge_uc) < min_cycles or len(charge_uc) == 0:
        return ERR_CYCLES, None

    ordered = sorted(charge_uc)
    count = len(ordered)
    if count % 2:
        median = ordered[count // 2]
    else:
        median = 0.5 * (ordered[count // 2 - 1] + ordered[count // 2])
    spread = 0.0 if median == 0.0 else \
        100.0 * (ordered[-1] - ordered[0]) / median
    return 0, {"cycles": count, "median_uc": median, "min_uc": ordered[0],
               "max_uc": ordered[-1], "spread_pct": spread,
               "energy_median_uj": median * volts}


def lifetime(spec):
    """Arithmetic only, with the uncovered part of the day costed."""
    if spec is None:
        return ERR_ARGUMENT, None
    for key in ("energy_per_cycle_uj", "cell_mah", "cell_volts",
                "sleep_volts", "cycle_period_s", "cycles_per_day"):
        if not spec[key] > 0.0:
            return ERR_SUPPLY, None
    if spec["sleep_ua"] < 0.0:
        return ERR_SUPPLY, None

    covered = spec["cycles_per_day"] * spec["cycle_period_s"]
    if covered > SECONDS_PER_DAY:
        return ERR_DAY, None
    uncovered = SECONDS_PER_DAY - covered

    cell_j = spec["cell_mah"] * 1e-3 * 3600.0 * spec["cell_volts"]
    cycle_j = spec["energy_per_cycle_uj"] * 1e-6 * spec["cycles_per_day"]
    uncovered_j = spec["sleep_ua"] * 1e-6 * spec["sleep_volts"] * uncovered
    return 0, {"cell_j": cell_j, "covered_s": covered,
               "uncovered_s": uncovered, "cycle_j_per_day": cycle_j,
               "uncovered_j_per_day": uncovered_j,
               "days": cell_j / (cycle_j + uncovered_j),
               "days_chapter": cell_j / cycle_j,
               "covers_the_day": 1 if uncovered == 0.0 else 0}


# ---------------------------------------------------------------------------
# Chapter 7 as printed, without NumPy so it can be run. Nothing above calls
# these.

def chapter_per_cycle_charge(current_ua, digital, fs, cycle_bit=0):
    """Step 8 exactly as printed, with np.diff and np.flatnonzero spelled out.

    Two things come out of running it. The rises it finds are the samples
    BEFORE each rise, so every window carries one sample of the previous cycle;
    on a flat trace that is invisible. And a trace beginning with the marker
    high has no rise in front of its first cycle, so the count is one lower
    than the number of marker pulses, which is what step 9's test gets wrong.
    """
    d = [(v >> cycle_bit) & 1 for v in digital]
    edges = [i for i in range(len(d) - 1) if d[i + 1] != d[i]]
    rises = [i for i in edges if d[i + 1] == 1]
    out = []
    for a, b in zip(rises[:-1], rises[1:]):
        out.append(sum(current_ua[a:b]) / fs)
    return out


def chapter_lifetime_days(energy_per_cycle_uj, cycles_per_day, cell_mah,
                          cell_v):
    """Step 10 exactly as printed.

    The arithmetic is right. What it does not contain is the rest of the day:
    cycles_per_day is a free assumption while the trace already fixes the cycle
    period, and nothing checks that the product is a day. Where it is not, the
    uncovered seconds cost nothing at all in this sum.
    """
    cell_j = cell_mah * 1e-3 * 3600.0 * cell_v
    return cell_j / (energy_per_cycle_uj * 1e-6 * cycles_per_day)
