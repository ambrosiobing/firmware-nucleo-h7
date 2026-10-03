# P02 in Python

**Nothing here, and not host only by accident.** The interpreter gives no control
over memory ordering at all: there is no way to ask for a release store, and the
global interpreter lock makes the question unanswerable rather than merely hard.

So this project's Python entry is a refusal with a reason, which is the honest
form of a variant that cannot be built. The ring is still exercised from Python,
in `python/tests/test_ring.py`, but that is the C implementation driven through
`ctypes` and it is the test harness rather than a Python implementation.


## What it is proven against

The four-mode soak: 21.4 million bytes through the ring with zero mismatches,
and a cycle and byte cost per ordering mode.