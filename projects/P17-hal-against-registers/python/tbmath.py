"""P17's timebase arithmetic in Python, the twin of c/tbmath.c.

The three acceptance criteria of chapter 17 that need no peripheral: the
instrument's own cost subtracted visibly, the first run kept apart from the
median of the remainder, and the timebase substitution with each wrap period
stated. Standard library only.

It also carries, at the bottom, the two pieces of chapter 17 that produce the
findings, written as printed. Neither is used above them:

* ``chapter_measure_run`` is step 6's subtraction on a 32-bit unsigned value,
  which wraps to a number near 2 to the 32nd when an interval is below the
  calibrated instrument cost.
* ``chapter_timebase_elapsed`` is an elapsed interval computed through step 8's
  struct, which carries a width and a frequency and no direction, so it can only
  subtract one way and is wrong for the one of its three peripherals that counts
  down.
"""
from __future__ import annotations

import math

TB_MAX_SAMPLES = 256
TB_COUNTS_UP = 0
TB_COUNTS_DOWN = 1

ERR_ARGUMENT = -1
ERR_WIDTH = -2
ERR_DIRECTION = -3
ERR_RANGE = -4
ERR_RATE = -5
ERR_UNDERFLOW = -6
ERR_SAMPLES = -7

ERRORS = {"argument": ERR_ARGUMENT, "width": ERR_WIDTH,
          "direction": ERR_DIRECTION, "range": ERR_RANGE, "rate": ERR_RATE,
          "underflow": ERR_UNDERFLOW, "samples": ERR_SAMPLES}


def _mask(width_bits):
    return 0xFFFFFFFF if width_bits >= 32 else (1 << width_bits) - 1


def elapsed(start, end, width_bits, counts_down):
    """Ticks between two readings, across a wrap and in either direction."""
    if width_bits == 0 or width_bits > 32:
        return ERR_WIDTH, None
    if counts_down not in (TB_COUNTS_UP, TB_COUNTS_DOWN):
        return ERR_DIRECTION, None
    mask = _mask(width_bits)
    if (start & mask) != start or (end & mask) != end:
        return ERR_RANGE, None
    if counts_down:
        return 0, (start - end) & mask
    return 0, (end - start) & mask


def wrap_ns(hz, width_bits):
    """The wrap period in nanoseconds, from the tick frequency and the width."""
    if width_bits == 0 or width_bits > 32:
        return ERR_WIDTH, None
    # Not finite is refused as well as at or below zero: an infinite tick rate
    # gives a wrap period of zero nanoseconds, which is a plausible number from
    # an impossible input. The C says the same at the same line.
    if not math.isfinite(hz) or hz <= 0.0:
        return ERR_RATE, None
    ticks = 1 << width_bits
    ns = float(ticks) * 1.0e9 / hz
    return 0, int(ns + 0.5)


def subtract_overhead(raw, overhead):
    """A raw interval less the instrument's cost, refusing rather than wrapping."""
    if raw < overhead:
        return ERR_UNDERFLOW, None
    return 0, raw - overhead


def report(samples):
    """The first run, and the median of the remainder, as two numbers."""
    if samples is None:
        return ERR_ARGUMENT, None
    if len(samples) > TB_MAX_SAMPLES or len(samples) < 2:
        return ERR_SAMPLES, None

    rest = sorted(samples[1:])
    count = len(rest)
    if count % 2:
        median = rest[count // 2]
    else:
        median = (rest[count // 2 - 1] + rest[count // 2]) // 2
    return 0, {"first": samples[0], "median": median, "smallest": rest[0],
               "largest": rest[-1], "count": count}


# ---------------------------------------------------------------------------
# Chapter 17 as printed. Nothing above calls either of these.

def chapter_measure_run(a, b, overhead):
    """Step 6's `return (b - a) - cyc_overhead` on a uint32_t.

    The calibration is the minimum over 64 back-to-back counter reads, which
    does not prevent a later interval from being smaller: a minimum is the floor
    of what was observed, not of what can happen. When it is smaller this wraps
    to a number near 2 to the 32nd, and at 280 MHz that is reported as just over
    fifteen seconds for an operation that took almost no time.
    """
    return ((b - a) - overhead) & 0xFFFFFFFF


def chapter_timebase_elapsed(start, end, width_bits):
    """An elapsed interval through step 8's struct, which has no direction.

    The struct carries a start function, a tick reader, a frequency and a width
    in bits. The comment beside its third instance says "counts down", and
    SysTick does. With no field for the direction the only subtraction available
    through the abstraction is this one, which for a down counter turns a short
    interval into nearly a whole wrap: on a 24-bit counter at 280 MHz, a
    hundred-tick interval reads as 59.92 milliseconds rather than 357
    nanoseconds.
    """
    mask = 0xFFFFFFFF if width_bits >= 32 else (1 << width_bits) - 1
    return (end - start) & mask
