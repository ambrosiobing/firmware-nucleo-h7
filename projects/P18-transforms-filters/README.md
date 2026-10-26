# P18: Transforms and filters with a numerical acceptance test

Status: c=host cpp=host python=host rust=host

**Started on the host, Monday 26 October 2026.** One biquad cascade in double,
in single and in Q15, over the same coefficients and the same input, plus the two
metrics chapter 18 judges them by. The chapter fixes the order of work itself:
"the acceptance test is written first, the tolerance is written down before
anything is measured, and only then does anyone look at cycle counts". The
tolerance is therefore the first deliverable, and whether a numeric format can
reach it is arithmetic on a stored vector, so it needed no board, no CMSIS-DSP
and no SciPy.

**What it is.** Output that must match a double-precision host reference within a
stated tolerance, with cycles per transform reported and the cache state named.

## What runs

| Piece | What proves it |
|---|---|
| `c/dspaccept.c`, three formats of one cascade and both metrics | 7 oracle cases, 5 metric cases, 8 refusals, and `-Wconversion -Wsign-conversion -Wdouble-promotion -Wpedantic` |
| `python/dspaccept.py`, the twin, standard library only | the same cases, and a pairwise comparison on the real design |
| `cpp/dspaccept.hpp`, one template at double and at float | the same cases through a filter, and the design vector three ways |
| `rust/src/lib.rs`, one macro body at both widths | the same cases through the same protocol, plus four unit tests |
| `vectors.json`, the oracle | **recomputed in exact rational arithmetic inside the test, on every run** |

**Six of the seven cascade cases are exact in all three formats at once.** Their
coefficients are powers of two and their inputs are Q15 integers chosen so that
no intermediate needs rounding, so the double path, the single path and the
fixed-point path must produce the same number and `python/tests/test_dspaccept_parity.py`
re-derives each of them with `fractions.Fraction` before comparing. No exact case
passes on a tolerance. The seventh case is a gain of three, which saturates Q15
where the float paths simply exceed one, and it is there because that difference
is the format and not a defect.

**The coefficients are checked without a design library.** A Butterworth
magnitude has a closed form, and the bilinear transform with prewarping maps it
exactly onto the digital axis, so

    |H(f)| = 1 / sqrt(1 + (tan(pi f / fs) / tan(pi fc / fs)) ** (2 * order))

verifies the committed sections at any frequency. They agree to 1.2e-11 relative
at eleven frequencies from 0 to 499 Hz. SciPy designed them on win11 aquamarine;
this is what lets a reader who does not have SciPy check them anyway, which also
means the CI runner can, with pytest as its only dependency.

## The float criterion passes, measured

| Criterion | Tolerance, written before the run | Measured |
|---|---|---|
| float32 against the double reference, relative RMS | 1e-5 | **2.388e-06, pass** |

## The Q15 criterion is decided by a scaling the chapter never states

Chapter 18 writes 55 dB before measuring anything, which is the right order. What
it does not say is how the passband gain is divided between the two sections, and
**that choice alone moves the same filter on the same vector across the
tolerance**:

| Scaling | Post shift | Signal to error | Saturated samples | Against 55 dB |
|---|---|---|---|---|
| the chapter's own export, step 3 | 2 | 15.02 dB | 0 | fail |
| gain split evenly, ratio 1.0 | 1 | 51.35 dB | 0 | fail |
| gain split, ratio 1.5 | 1 | 54.37 dB | 0 | fail |
| gain split, ratio 2.0 | 1 | **55.24 dB** | **16** | pass |
| gain split, ratio 2.5 | 1 | 16.55 dB | 219 | fail |

Step 3 exports the design library's sections exactly as printed, which leaves the
whole passband gain in section one: `b0` there is 1.832e-4 while `b1` of section
two is 2.0. Those two cannot share a Q15 scale. At the post shift the large
coefficient forces, the small ones quantise to **2, 3 and 2 parts in 32768**, and
the cascade measures 15.02 dB, forty below the tolerance. Nothing about the
arithmetic is wrong; the scaling was never chosen.

**The one scaling that clears the tolerance clears it with sixteen saturated
samples**, and the chapter's acceptance script has nowhere to print them. So
`biquad_df1_q15` counts saturations and will not accept a null pointer for the
count: a Q15 verdict without it is not a verdict. The post shift is reported the
same way, by `q15_post_shift` being a separate call whose answer belongs in the
report beside the number it decided, exactly as the chapter already requires of a
cycle count.

**The chapter's own metric divides by zero** on any case where the two paths
agree exactly, which is every exact case in this oracle: step 9's `snr_db`
divides by the error energy with no test for it. `accept_metrics` reports an
`exact` flag and an infinity instead, and a test calls the chapter's version to
show it raising rather than describing it. Both sketches, the metric and the
export, are carried unused in `python/dspaccept.py` so the comparison is against
the real thing.

## What is not here

The real transform and its packed output, where the zero-frequency and Nyquist
bins share the first two floats; the complex magnitude; the cycle counter; the
eight-case grid of two formats, two code placements and two data placements with
both cache states swept inside each; and the ITCM and DTCM linker sections that
the grid exists to compare. **There is no measured cycle anywhere in this project**,
and the status line says host for that reason. The 64-bit accumulator here also
isolates coefficient and state quantisation from accumulator width, and the
library's Q15 routine accumulates in 32 bits, so a cascade that saturates its
accumulator on the part can differ from this. That is a bench measurement and is
not claimed either way.

**What it needs on the bench.** Nucleo alone.

Chapter 18 of [the book][book] is the written design.

[book]: ../../chapters/18-transforms-and-filters-with-a-numerical-acceptance-test.md
