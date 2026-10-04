# P12 in C

**State: host, since Sunday 4 October 2026.** Written on win11 aquamarine, which
compiles nothing; the first compiler to see it was gcc 15 in WSL on the win11
skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and then CI with warnings as errors.

| File | What it is |
|---|---|
| `gates.h` | the two gates, and the three kinds of refusal that are faults in the call rather than verdicts on the measurement |
| `gates.c` | the rules |

## This is the one of the four that could compile for the target

It allocates nothing and it has no global state. Whether it ever should is a
different question and the answer is no: the gate runs on a laptop or on the
Raspberry Pi beside the board, reading a ledger the nRF PPK2 produced, so there
is nothing for it to do on the board itself. The property is worth having anyway,
because it is what forced the file's shape.

**What that shape costs, said plainly.** The answer is a string of unknown length
built from a map of unknown size, and with no allocator that means fixed buffers,
a cursor that refuses rather than truncates, and an insertion sort over an array
of pointers. That is about sixty lines the C++ and the Rust do not have, and the
comparison is not flattering to C: those sixty lines are where a defect would
live, and three of the four implementations cannot have that defect at all.

**The refusal rather than the truncation is the part worth keeping.** A truncated
answer would be compared against three full ones and read as a disagreement about
the gate, which is the most misleading thing this file could produce. A name that
does not fit its array is refused for the same reason: two truncated names could
collide, and the gate would then compare one target's budget against another's
binary.

## What it is proven against

Twenty eight cases whose verdict is known by construction, driven through every
implementation by `python/tests/test_gates_parity.py`. Eleven exercise the size
gate and seventeen the charge gate, and between them they reach every refusal and
every failure the protocol defines, which the test asserts separately in Python
so that assertion runs on a laptop with no compiler.

The case that matters is the one the chapter states outright: **six percent on
one phase, which every implementation must fail and must name the phase of.** A
gate that called that a pass would be worthless, and four implementations
agreeing on a wrong answer would still be wrong. The boundary is pinned from the
other side too, by a case at four and a half percent that every implementation
must pass, because a gate that failed everything would also satisfy the first.

Three kinds of case are there for the order of a decision rather than its
content. A ledger with no build identity **and** a total that does not reconcile
must be refused for the identity, because a capture that cannot be tied to a
firmware is not evidence whatever its numbers say. A target over both its budgets
must report flash before static_ram. And two cases are handed over deliberately
unsorted, so an implementation that did not order by name would disagree there
and nowhere else.

[../PROTOCOL.md](../PROTOCOL.md) is the contract, written before these
implementations rather than inferred from whichever was read first. It records
why the comparison is on the rules with the case already parsed rather than on
JSON, and why the doubles in an answer are written in a round-tripping form
rather than the two decimal places each gate prints for a person.

## What no parity test can tell you

Whether the numbers in `../baseline.json` mean anything. Every one is a
placeholder, the file says so in its own `_note`, and a test asserts that it says
so. Four implementations agreeing on a comparison against a placeholder is still
a comparison against a placeholder, and what it buys is that the gate will be
right on the day there is a measurement to put there. That day needs the board,
the nRF PPK2 and the Raspberry Pi beside it.
