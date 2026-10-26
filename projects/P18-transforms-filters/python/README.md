# P18 in Python

**State: host, since Monday 26 October 2026.** The twin of
[`../c/dspaccept.c`](../c/dspaccept.c): the same algorithm, the same refusals and the
same numbers, in the standard library alone. The suite's whole requirements file
is one line, and a check that needed a numerical package to decide a pass would
not run on the runner that decides merges.

| File | What it is |
|---|---|
| `dspaccept.py` | one biquad cascade in double, in single and in Q15, and both acceptance metrics |

**This file carries both of chapter 18's sketches, unused.** `chapter_snr_db` is step 9's metric, which divides by the error energy with no test for it and so divides by zero on every exact case in the oracle. `chapter_export_rows` is step 3's coefficient export, which hands a design library's sections straight to the converter and is the reason the Q15 path measures 15.02 dB. Tests call both, so the finding is a demonstration rather than a sentence.

## What it is proven against

7 cascade cases, 6 of them exact in all three formats at once and re-derived in exact rational arithmetic inside the test on every run, plus 5 metric cases and 8 refusals, driven through this implementation and the C by
`python/tests/test_dspaccept_parity.py`. This half runs everywhere, including on a
laptop with no compiler, which is why the oracle is checked here first.

## What is not here

The real transform and its packed output, the complex magnitude, the cycle counter, the eight-case grid of two formats by two code placements by two data placements with both cache states swept inside each, and the ITCM and DTCM sections the grid exists to compare. **There is no measured cycle in this project.** The 64-bit accumulator here also isolates coefficient and state quantisation from accumulator width, and the library's Q15 routine accumulates in 32 bits, so a cascade that saturates its accumulator on the part can differ from this. That is a bench measurement and is not claimed either way.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
