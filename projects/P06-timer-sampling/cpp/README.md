# P06 in C++

**State: written Saturday 3 October 2026, and not yet built.** Written on win11
aquamarine, which compiles nothing; the first compiler to see it is `g++` in WSL
on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and then CI with warnings as errors.

| File | What it is |
|---|---|
| `rate.hpp` | the whole witness, header only |
| `rate_filter.cpp` | the filter the parity test drives, one verb: `A <path> <fs> <nominal>` |

## What C++ adds here, and it is very little

That is the honest answer and it is worth giving plainly. The witness is
floating-point arithmetic over an array, which C expresses as well as C++ does.
`std::optional` makes a refusal a different type rather than a negative return
code, and `std::array` fixes the buffer sizes without a macro. That is the whole
list.

There is deliberately no `constexpr` self-test, unlike P05's and P03's. The
arithmetic involves `sqrt` on values a compile-time case would have to carry as a
literal, and a hand-computed expected double would be a second implementation of
the thing under test rather than a check on it.

## The filter takes a path, which no other filter here does

A capture is thousands of doubles. Inline it would be a line of tens of
kilobytes, which is exactly what broke the P08 filter earlier the same day. And a
path is how the real witness receives a capture: `rate.py` takes one on its
command line. Doubles are printed with seventeen significant digits, which
round-trips a double exactly, so the comparison's tolerance is a property of the
arithmetic and not of the printing.

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
