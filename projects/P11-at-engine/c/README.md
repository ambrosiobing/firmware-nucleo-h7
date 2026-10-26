# P11 in C

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI, which rebuilds the same
sources with warnings as errors.

| File | What it is |
|---|---|
| `atparse.h` | the line classes, the wrap-safe deadline, and the backoff with both of its bounds |
| `atparse.c` | the classifier, the deadline test and the backoff |

**The deadline test is the piece a board could not prove.** Chapter 11 writes it as a signed difference because the obvious comparison is correct for forty-nine days and then is not, and a board would have to run for forty-nine days to show it. A walk across the wrap requires exactly one flip, at the deadline, and a falsifier counts the cases the naive comparison gets wrong.

## What it is proven against

36 oracle cases, 17 for the classifier, 9 for the deadline and 10 for the backoff, plus four property tests, driven through this implementation and its Python twin by
`python/tests/test_atparse_parity.py`. The C half of that test skips on a laptop
with no compiler, naming the WSL command, and runs in full in CI.

## What is not here

The engine, its queue, the hundred injected faults and the no-SIM stop. All four need the cellular module, which the inventory does not hold.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
