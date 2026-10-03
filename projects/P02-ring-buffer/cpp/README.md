# P02 in C++

**State: host only, proven Saturday 3 October 2026.** Written on win11
aquamarine, which compiles nothing, then built and proven the same day: `g++` in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and then CI at commit 3b301e2, where it is
also compiled with the full warning set and `-Werror`.

It compiled without a correction on the first attempt, which had not happened
for a C++ or Rust file in this volume before: P09's needed a release profile
moved, P08's needed its filter rewritten. The template with `if constexpr`
selecting the primitives per mode is the part that could have gone wrong and did
not.

| File | What it is |
|---|---|
| `ring.hpp` | the ring, header only, with the ordering mode as a template parameter |
| `ring_filter.cpp` | the filter the parity test drives, four verbs: `I`, `P`, `G`, `S` |

## What C++ adds here, and it is more than in P08

The C selects its ordering with `-DRING_BARRIER` and builds four separate
objects, so a reader comparing two modes compares two builds and a measurement
table has four rows from four images. Here the mode is a template parameter, so
`Ring<Barrier::None>` through `Ring<Barrier::AcqRel>` are four types that exist
side by side in one translation unit, and `if constexpr` selects the primitives
so each one still emits exactly the barriers it asks for.

That is the one place in this volume where C++ offers something the C genuinely
cannot: four variants of one algorithm without four builds and without the
preprocessor. The filter holds all four at once for that reason.

It changes nothing about the measurement. On the board each mode is still its own
image, because a cycle count wants one image doing one thing.

## What is deliberately not here

`volatile`, which appears in most published versions of this structure and is not
sufficient. It orders volatile accesses against each other and says nothing about
the ordinary store to the data array that must be visible before the index
publishing it. The standard gives no ordering between a volatile and a
non-volatile access.

The methods take `*this` by mutable reference, so this header's own use is
sequential. A genuine two-context split, a producer handle and a consumer handle
over one buffer, is a different shape in every language and is the board half's
subject rather than this file's.

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
