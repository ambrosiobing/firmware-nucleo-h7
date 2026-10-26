# P18 in C

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI, which rebuilds the same
sources with warnings as errors.

| File | What it is |
|---|---|
| `dspaccept.h` | the coefficient order, the two metrics, the post-shift bound and the six refusals |
| `dspaccept.c` | one biquad cascade in double, in single and in Q15, and both acceptance metrics |

**Three formats, one set of coefficients, one input.** The double path is the reference the chapter says the comparison must stand on; the single path is the format the part has hardware for; the Q15 path is the one the chapter's tolerance turns out not to settle, because dividing the same filter's passband gain differently between its two sections moves the measurement from 15.02 dB to 55.24 dB against a tolerance of 55. The post shift and the saturation count are therefore reported rather than inferred, and `biquad_df1_q15` will not accept a null pointer for the count.

## What it is proven against

7 cascade cases, 6 of them exact in all three formats at once and re-derived in exact rational arithmetic inside the test on every run, plus 5 metric cases and 8 refusals, driven through this implementation and its Python twin by
`python/tests/test_dspaccept_parity.py`. The C half of that test skips on a laptop
with no compiler, naming the WSL command, and runs in full in CI.

## What is not here

The real transform and its packed output, the complex magnitude, the cycle counter, the eight-case grid of two formats by two code placements by two data placements with both cache states swept inside each, and the ITCM and DTCM sections the grid exists to compare. **There is no measured cycle in this project.** The 64-bit accumulator here also isolates coefficient and state quantisation from accumulator width, and the library's Q15 routine accumulates in 32 bits, so a cascade that saturates its accumulator on the part can differ from this. That is a bench measurement and is not claimed either way.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
