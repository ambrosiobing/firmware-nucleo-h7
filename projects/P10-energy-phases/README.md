# P10: Where the energy goes, wake, sense, compute, send

Status: c=host cpp=none python=host rust=host

**Started on the host, Monday 26 October 2026.** A current trace plus a marker
code per sample go in; the charge drawn during each phase comes out, with the
total integrated separately and the residual between the two. All of that is
arithmetic on a uniformly sampled trace, so it is exact on synthetic input and
needed no power profiler to settle.

**What it is.** Marker pins and per-phase charge accounting, so a duty cycle's
energy can be attributed to wake, sense, compute and send rather than quoted as
one number.

## What runs

| Piece | What proves it |
|---|---|
| `c/phaseledger.c`, the decode, the integration and the residual | 13 oracle cases, five property tests, and `-Wconversion -Wsign-conversion -Wdouble-promotion -Wpedantic`, which CI passes over it |
| `python/phaseledger.py`, the twin | the same cases, and a pairwise comparison |
| `rust/src/lib.rs`, `no_std` with `f64`, and `rust/src/main.rs`, the filter | the same cases through a one verb protocol, and six unit tests |
| `vectors.json`, the oracle | **recomputed in exact rational arithmetic**, which neither implementation uses |

**Every current in the oracle is a sum of powers of two and dt is 2^-10 seconds**,
so every charge is exact in binary and no case passes on a tolerance. A
disagreement is an implementation being wrong rather than a rounding argument.

## The residual is the test, and it is meant to be non-zero sometimes

Chapter 10's step 8 computes the total twice by different routes on purpose, and
says why: "if the residual were defined as the difference between the phase sum
and itself it would always be zero and the test would prove nothing". When a run
is rejected as too short its charge stays in the total and leaves the split, so
the residual rises. **One pair of cases shows exactly that**: the same trace at a
floor of two samples has a residual of two thirds, and at a floor of one has a
residual of zero. The count of rejected samples is reported beside it, which is
what turns step 9's "attribute the residual" into something checkable.

## A correction to the chapter, demonstrated rather than asserted

**Step 7's decoder rejects runs by comparing seconds, and the comparison is
decided by floating-point rounding.** It filters on `(iv[2] - iv[1]) >= dt` where
the interval ends are products `i * dt`. On a ten sample trace with dt = 1e-5 it
builds seven runs, five of them one sample long, and **keeps two of those five
while rejecting three**, because the differences land either side of dt in the
last bits:

| run | length in samples | `t1 - t0` | kept |
|---|---|---|---|
| code 0 at sample 0 | 1 | 1.0000000000000001e-05 | yes |
| code 3 at sample 4 | 1 | 9.9999999999999991e-06 | no |
| code 7 at sample 9 | 1 | 9.9999999999999991e-06 | no |

The physical question the filter means to ask is whether a run is shorter than
the instrument can resolve, which is a question about a count of samples. **So
this code counts samples as integers and takes the floor in samples.** The
chapter's sketch is carried unused in `python/phaseledger.py` so a test can
demonstrate the behaviour against the real thing rather than against a paraphrase,
and that test fails if the sketch ever becomes consistent.

## What is not here

The marker protocol on the board: three pins, one store to the set and reset
register per change, and the bound on what a marker costs, which has to be inside
the report because the markers are inside the thing being measured. The capture
itself, the three bench questions about the profiler's digital inputs, and the
experiment the ledger exists for, which is whether it pays to race to sleep.
**There is no measured number anywhere in this project yet**, and the status line
says host for that reason.

**What it needs on the bench.** Nucleo plus the nRF PPK2, whose digital inputs put
the marker edges on the same time axis as the current.

Chapter 10 of [the book][book] is the written design.

[book]: ../../chapters/10-where-the-energy-goes.md
