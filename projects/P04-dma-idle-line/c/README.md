# P04 in C

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI, which rebuilds the same
sources with warnings as errors.

| File | What it is |
|---|---|
| `rxspan.h` | the two spans, the positions, and the refusals |
| `rxspan.c` | the span arithmetic, which is unsigned modular subtraction and nothing else |

**Why the C is the one that matters here.** The decode subtracts two transfer-counter positions, and the case that is easy to get wrong is the one where the counter has wrapped past the end of the buffer between the two readings. In C that subtraction is defined behaviour on unsigned types, so the wrap needs no conditional at all, and the file is written that way deliberately: a version with a branch for the wrap would have a case that the oracle has to reach, and this one does not.

## What it is proven against

18 oracle cases, written by hand from the geometry rather than generated, and reaching both the wrapped and the unwrapped span, driven through this implementation and its Python twin by
`python/tests/test_rxspan_parity.py`. The C half of that test skips on a laptop
with no compiler, naming the WSL command, and runs in full in CI.

## What is not here

The transfer engine itself, the idle-line interrupt, the half-transfer and complete callbacks, and every byte that actually arrives. Those need the board and RM0455's transfer engine chapter.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
