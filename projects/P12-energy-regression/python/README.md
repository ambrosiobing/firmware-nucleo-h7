# P12 in Python

**State: host only, host by nature rather than by omission, and the reference the
other three are compared against.**

`gates.py` is the two gates, in increasing order of what they cost to run, so the
cheap one fails first. The fixture files it reads are in this project directory,
and `python/tests/test_gates.py` proves that a five percent charge regression
turns the build red with nobody at the bench.

This is the one project where Python is the deliverable rather than a twin: the
gate runs on a continuous integration runner, which has no board.

| File | What it is |
|---|---|
| `gates.py` | the two gates, their English, and the command line a CI job invokes |

## Why the Python is the reference here and the C is everywhere else

The other five parity tests take the C as the reference because the C is the one
that compiles for the target. Here the reason runs the other way: **the gate that
actually runs is this one.** It reads the committed `../baseline.json` and
`../budgets.json`, it is what a CI job invokes, and the other three exist to show
the rules are stated clearly enough to be implemented four times.

## One change the parity work made to this file, and it is an improvement

Each gate is now two functions where it was one. `size_verdict` and
`charge_verdict` return the decision as data; `check_sizes` and `check_charge`
render the English from it and raise as they always did. Nothing a caller sees
changed, and `test_gates.py`'s sixteen checks passed before and after without
being touched.

The reason is that the oracle had to be expressible. A verdict that exists only
as an English sentence cannot be compared with three other implementations
without parsing prose, and parsing prose would have made the comparison about
wording. The gain is more than the parity test: there is now one set of rules
rather than a set of rules and a set of sentences that describe them, so the
wording cannot drift away from the verdict it describes.

## What stays here and is in no other language

The JSON edge, the committed files, and the English. The other three
implementations take a case already parsed, which
[../PROTOCOL.md](../PROTOCOL.md) explains, so nothing in them reads a file or
writes a sentence a person would act on. `test_gates.py` is what proves this
half, and the parity test does not replace it.

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
