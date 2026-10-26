# P11 in Python

**State: host, since Monday 26 October 2026.** The twin of
[`../c/atparse.c`](../c/atparse.c): the same algorithm, the same refusals and the
same numbers, in the standard library alone. The suite's whole requirements file
is one line, and a check that needed a numerical package to decide a pass would
not run on the runner that decides merges.

| File | What it is |
|---|---|
| `atparse.py` | the classifier, the deadline test and the backoff |

**Two findings came out of reading step 8 closely**, and both are pinned by cases here: the cap bounds the doubling rather than the result, so the jittered delay can exceed it by an eighth, and a cap above half the range overflows the doubling, which is refused.

## What it is proven against

36 oracle cases, 17 for the classifier, 9 for the deadline and 10 for the backoff, plus four property tests, driven through this implementation and the C by
`python/tests/test_atparse_parity.py`. This half runs everywhere, including on a
laptop with no compiler, which is why the oracle is checked here first.

## What is not here

The engine, its queue, the hundred injected faults and the no-SIM stop. All four need the cellular module, which the inventory does not hold.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
