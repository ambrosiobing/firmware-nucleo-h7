# P04 in Python

**State: host, since Monday 26 October 2026.** The twin of
[`../c/rxspan.c`](../c/rxspan.c): the same algorithm, the same refusals and the
same numbers, in the standard library alone. The suite's whole requirements file
is one line, and a check that needed a numerical package to decide a pass would
not run on the runner that decides merges.

| File | What it is |
|---|---|
| `rxspan.py` | the span arithmetic, which is unsigned modular subtraction and nothing else |

**What the twin is for.** The oracle was written against this implementation rather than derived from the C, because a model of a transfer counter is cheap in Python and a hand-written oracle needs somewhere to be checked before the C exists.

## What it is proven against

18 oracle cases, written by hand from the geometry rather than generated, and reaching both the wrapped and the unwrapped span, driven through this implementation and the C by
`python/tests/test_rxspan_parity.py`. This half runs everywhere, including on a
laptop with no compiler, which is why the oracle is checked here first.

## What is not here

The transfer engine itself, the idle-line interrupt, the half-transfer and complete callbacks, and every byte that actually arrives. Those need the board and RM0455's transfer engine chapter.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
