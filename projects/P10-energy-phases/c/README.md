# P10 in C

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI, which compiles the same
source with that whole warning set. It is not in the workflow's
warnings-as-errors step yet: it compiles clean under `-Werror` on gcc 15.2.0,
and adding it there turned the runner's own gcc red on Monday
26 October 2026, so that step waits on the diagnostic.

| File | What it is |
|---|---|
| `phaseledger.h` | the protocol's eight codes, the refusals, and the correction to step 7 at length |
| `phaseledger.c` | the run-length decode by sample index, the two totals by different routes, and the residual between them |

**The one place this file departs from the chapter** is in the header rather than hidden in the code: step 7 rejects short runs by comparing differences of seconds, and that comparison is decided by floating-point rounding. Run lengths here are counts of samples, which is the question the filter was reaching for.

## What it is proven against

13 oracle cases recomputed in exact rational arithmetic, with every current a sum of powers of two and dt at 2^-10 seconds, so no case passes on a tolerance, plus five property tests, driven through this implementation and its Python twin by
`python/tests/test_phaseledger_parity.py`. The C half of that test skips on a laptop
with no compiler, naming the WSL command, and runs in full in CI.

## What is not here

The marker protocol on the board, the single store to the set and reset register per change, the bound on what a marker costs, the capture, and the race-to-sleep experiment the ledger exists for. **There is no measured number in this project.** All of it needs the nRF PPK2, whose digital inputs put the marker edges on the same time axis as the current.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
