# P10 in Python

**State: host, since Monday 26 October 2026.** The twin of
[`../c/phaseledger.c`](../c/phaseledger.c): the same algorithm, the same refusals and the
same numbers, in the standard library alone. The suite's whole requirements file
is one line, and a check that needed a numerical package to decide a pass would
not run on the runner that decides merges.

| File | What it is |
|---|---|
| `phaseledger.py` | the run-length decode by sample index, the two totals by different routes, and the residual between them |

**This file also carries the chapter's sketch, unused.** `chapter_sketch_intervals` is step 7 exactly as printed, and it is here so a test can demonstrate against the real thing what is wrong with it rather than against a paraphrase. That test fails if the sketch ever becomes consistent.

## What it is proven against

13 oracle cases recomputed in exact rational arithmetic, with every current a sum of powers of two and dt at 2^-10 seconds, so no case passes on a tolerance, plus five property tests, driven through this implementation and the C by
`python/tests/test_phaseledger_parity.py`. This half runs everywhere, including on a
laptop with no compiler, which is why the oracle is checked here first.

## What is not here

The marker protocol on the board, the single store to the set and reset register per change, the bound on what a marker costs, the capture, and the race-to-sleep experiment the ledger exists for. **There is no measured number in this project.** All of it needs the nRF PPK2, whose digital inputs put the marker edges on the same time axis as the current.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
