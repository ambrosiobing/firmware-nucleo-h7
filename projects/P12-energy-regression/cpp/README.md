# P12 in C++

**State: host, since Sunday 4 October 2026.** Written on win11 aquamarine, which
compiles nothing; the first compiler to see it was g++ 15 in WSL on the win11
skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and then CI with warnings as errors.

| File | What it is |
|---|---|
| `gates.hpp` | the two gates, header only |
| `gates_filter.cpp` | the filter the parity test drives, two verbs: `S` and `C` |

## What C++ adds here, and this time it is more than it added to P06

P06's witness was floating-point arithmetic over an array, and the honest answer
there was that C++ added almost nothing. This gate is different, and two of the
three things it adds are about saying what the C can only document.

`std::optional<long>` makes "the build reported no static_ram" a different type
from "the build reported zero". In the C that distinction is a `bool` beside a
`long`, and it holds only as long as every reader checks the bool first. That
distinction is not decoration: a build that stopped reporting a field would
otherwise look like a build that uses no memory, which is the same class of error
as a lost phase in the charge gate and flatters the work the same way.

`std::map` iterates in name order, which is what the rules require, so the
ordering the C does with an insertion sort over pointers is simply a property of
the container here. And a verdict is a returned value rather than a buffer the
caller provides and a status code it must check.

**What it costs.** This file allocates, which is why the C and not this one is
the version that could compile for the target. The gate runs on a host, so it is
not a cost this project pays, but the asymmetry is real and is recorded rather
than glossed.

## The filter reads all of stdin before answering anything

Not a line at a time into a fixed buffer, and that is a lesson rather than a
preference. P08's filter used a 4096-byte buffer, met a request of twenty one
thousand characters on Saturday 3 October 2026, and answered twenty requests with
twenty five answers: it had split one long line into several, and each fragment
parsed into something. A filter that quietly turns one request into several is
the worst failure available to it, because the parity test then compares
misaligned answers and reports a disagreement about the gate.

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
