# P02 in Python

**State: host only, proven Saturday 3 October 2026.** It needs no compiler, so
it went from written to proven in one run on win11 aquamarine: it agrees with the
C over all three operation lists in all four ordering modes, 9620 operations
each.

| File | What it is |
|---|---|
| `ring.py` | the structure: the invariant, the capacity policy, the drop count, and the counters |

## This narrows an earlier claim rather than reversing it

Until Saturday 3 October 2026 this directory said there was no Python
implementation at all, and gave as the reason that the interpreter offers no
release store, no acquire load and no data memory barrier, and that the global
interpreter lock makes the ordering question unanswerable rather than merely
hard.

Every word of that is still true, and it is still the reason Python is absent
from the ordering comparison. It was too broad as a reason to write nothing: the
container is not the ordering. The invariant and the arithmetic are
language-independent claims, and a fourth independent implementation of them is a
fourth chance to catch a defect in the other three.

So, precisely:

| | |
|---|---|
| joins | the trace comparison: FIFO order, capacity, drop counting, the counters across their wrap |
| cannot join | the ordering comparison, because the four modes do not exist here |
| cannot join | any cycle or byte cost, because those need the board |

## The one part that needs care, and it is the opposite of C's

The free-running counters. In C they are `uint32_t` and the wrap is defined
unsigned arithmetic, which is exactly what makes `head - tail` correct across it.
Python integers do not wrap at all, so every update is masked to 32 bits
explicitly and so is the subtraction.

A version that forgot the mask would agree with the C for the first four billion
bytes and then diverge. That is not a hypothetical: removing the mask on purpose
is one of the three mutations the parity test was proven able to catch, and it is
the one the trace lists miss entirely.

## What it is proven against

P02's oracle is a trace, not a value: for one list of operations, which bytes
are accepted, which are refused and counted as drops, the order they come back
in, and the two free-running counters.
`python/tests/test_ring_parity.py` drives every implementation over three lists,
9620 operations, in each of the four ordering modes, and compares every answer
against the C.

The counters are in the comparison on purpose. `used` is derived from
`head - tail`, so an implementation that wrongly masked its counters to the
capacity would agree on `used` forever and disagree on `head` at once. A fourth
comparison therefore drives the counters past their own 32-bit wrap, which the
trace lists cannot reach in a reasonable number of operations.

**All four modes must give identical traces.** A barrier constrains visibility
between two contexts and says nothing about one context's own sequence, so a
single-threaded trace that changed with the mode would mean a mode was touching
the data path rather than the ordering. The C builds four objects, the C++
instantiates a template four times and the Rust monomorphises a const generic;
the test checks that the three routes arrive at the same place.

Proven able to fail three ways, one per claim: the capacity reduced by one,
which turned the fill list red at operation 256 naming both answers; the drop
increment removed, red at operation 258; and the 32-bit mask removed from the
Python counter, which the trace lists do **not** catch, because the first four
billion operations agree, and which the wrap comparison caught at byte 3 with
"the C head is 0 and the Python head is 4294967296".

## What no host can compare

The cost of each ordering mode, which is P02's actual subject. Those are board
figures, measured with the instruction cache on: the compiler fence costs 0.00
cycles and 0 bytes while still emitting different instructions, one DMB in put
and get costs 22.00 cycles and 16 bytes, and acquire plus release costs 29.00
and 24. Nothing in this directory can produce or check any of that.
