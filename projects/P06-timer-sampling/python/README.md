# P06 in Python

**State: host only, and the reference the other three were written against.**
`python/firmkit/rate.py` carries the witness and `scan.py` drives it over a
capture. Both were written before any of the other implementations existed.

| File | What it is |
|---|---|
| `scan.py` | the rate scan: drive a capture through the witness and report |
| (shared) `python/firmkit/rate.py` | the witness itself, because P18 will want it too |

The witness lives in `firmkit` rather than here because more than one project
needs it, which is the same rule the C follows with `c/ring` and `c/payload`.

## Why Python is a reasonable language for this half

The witness reads a recording an instrument produced and decides whether a rate
claim stands. It runs on a host by nature, and the arithmetic is a least-squares
fit and a standard deviation, which Python expresses as clearly as anything.

The one place it would be the wrong choice is the board, and there it is not a
candidate at all: MicroPython would carry the arithmetic and would not carry the
timing the acquisition depends on.

## What it is proven against

Nine synthetic captures whose answers are known by construction, written to
files because a capture is thousands of doubles, and driven through every
implementation by `python/tests/test_rate_parity.py`:

| Capture | What every implementation must say |
|---|---|
| a clean kilohertz square wave | pass, and 1000.014 Hz from the fit |
| **a clean wave at 1100 Hz** | **fail on the rate, pass on the jitter.** This is the one the project exists for: a clean wave at the wrong rate, which a witness that called it a pass would be worthless for |
| jitter of two microseconds | pass, standard deviation 7.4 microseconds against the ten the criterion allows |
| jitter of thirty microseconds | fail on jitter, pass on the rate, because the mean period is unchanged |
| one dropped edge | one missing edge, from an interval near twice nominal |
| one doubled edge | two missing edges, from two intervals near a half |
| offset and attenuated | identical to the clean capture, which is what proves the thresholds come from the observed swing and not from assumed rail voltages |
| a flat recording | refused for having no swing, rather than reported as perfect |
| a fragment | refused for having too few edges |

**Why this comparison is numeric where the other four are exact.** The other
parity tests compare bytes, row indices or integers and demand equality. This one
compares doubles to a relative tolerance of 1e-12. Double arithmetic is
deterministic, but the contraction of a multiply and an add into one fused
instruction is not the same across four toolchains: gcc may contract where rustc
does not, and the Cortex-M7's floating point unit has a fused multiply-add where
a host may not. The C and C++ are compiled with `-ffp-contract=off` to remove
the question where it can be removed, and the tolerance covers what remains. The
pass criteria are 0.1 percent, so 1e-12 is nine orders of magnitude tighter than
anything the measurement claims.

The booleans and the counts are compared exactly. A verdict that differed between
implementations would be a real disagreement whatever the arithmetic did.

## What no host can compare

Whether the board actually samples at 1 kHz. That is what the witness is for, and
it needs the board, the marker pin and the MCC 118. The three acquisition back
ends still refuse at run time rather than guessing a converter, timer or transfer
engine setting RM0455 governs.
