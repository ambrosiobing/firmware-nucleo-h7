# P12's parity protocol: one case in, one verdict out

The four implementations of these two gates are compared by
`python/tests/test_gates_parity.py`. This page is the contract they all obey, and
it is written down rather than inferred from whichever implementation was read
first.

## Why a line protocol and not JSON

**The gate is the subject of this project. JSON is not.** The real gate reads a
ledger and a baseline from JSON files, and the Python does exactly that. Asking
the C, the C++ and the Rust to do the same would mean three hand-written JSON
parsers, and those parsers would become the thing under test: a divergence would
far more likely be a disagreement about escapes or number syntax than about
whether a six percent charge regression turns a build red.

So the four implementations are compared on the rules, with the case already
parsed, which is the arrangement the other five parity tests use. JSON stays at
the Python's edge, where the gate that actually runs needs it, and
`python/tests/test_gates.py` is what proves that edge works on the committed
files.

## One request per line, one answer per line

Every field is free of spaces, so the request splits on whitespace. `-` means
absent throughout, which is a real state in both gates rather than a formatting
convenience: a build that reports no `static_ram` is a different case from one
that reports zero.

### The size gate

    S <measured> <budgets>

`<measured>` and `<budgets>` are `-` for an empty map, or items joined by `,`:

    <name>:<flash>:<static_ram>

where either number may be `-`, meaning the key is not present at all.

### The charge gate

    C <tolerance> <build> <total> <phases> <baseline>

`<tolerance>` is a fraction, `0.05` for the five percent the chapter states.
`<build>` is the build identity or `-` for a ledger that carries none. `<total>`
is the reported total in microcoulombs or `-` for a ledger that carries none.
`<phases>` and `<baseline>` are `-` for empty or items joined by `,`:

    <name>:<microcoulombs>

### The answer

    <PASS|FAIL> <refusal> <failures> <report>

`PASS` only when there is no refusal and no failure.

`<refusal>` is `-`, or the one refusal that stopped the gate before it examined
anything else:

| Refusal | Gate | Meaning |
|---|---|---|
| `no_sizes` | size | nothing was reported, which is a build failure wearing a passing gate's clothes |
| `no_build` | charge | the ledger carries no build identity, so it is not evidence |
| `no_phases` | charge | the ledger or the baseline carries no phases |
| `no_total` | charge | there is no reported total to reconcile the phases against |
| `sum_mismatch:<summed>:<total>` | charge | the phases do not sum to the reported total, so a phase is missing or double counted |

**The order of the charge gate's four refusals is part of the rules.** A ledger
with no build identity is refused before its phases are looked at, because a
capture that cannot be tied to a firmware is not evidence whatever its numbers
say. The sum check comes before any phase is compared, because a run whose parts
do not reconcile has lost or double counted a phase and no verdict on it is worth
anything. An implementation that named a different first refusal for the same
ledger would be a real disagreement, so the answer carries which refusal came
first and the parity test compares it.

`<failures>` is `-`, or items joined by `,` **in the order the gate produces
them**, which is the baseline's phases in name order and then the measured phases
in name order. The order is compared, not just the set, because it is what a
reader of the failure sees first.

| Failure | Gate | Fields |
|---|---|---|
| `no_budget:<target>` | size | a target in the build with no committed budget |
| `no_field:<target>:<field>` | size | the build reported no such field |
| `no_field_budget:<target>:<field>` | size | no budget committed for that field |
| `over:<target>:<field>:<used>:<limit>:<excess>` | size | three integers |
| `missing_phase:<phase>` | charge | the baseline has it and the ledger does not |
| `over:<phase>:<got>:<want>:<change>` | charge | three doubles |
| `not_in_baseline:<phase>` | charge | measured, and the gate is not watching it |

`<report>` is `-`, or items joined by `,` in the same production order:

| Gate | Item |
|---|---|
| size | `<target>:<field>:<used>:<limit>:<pct>:<margin>` |
| charge | `<phase>:<got>:<want>:<change>` |

## Numbers in the answer, and why they are not the gate's own formatting

Integers are written as integers and compared exactly. **Doubles are written in
whatever form round-trips the double exactly in that language, and are compared
to a relative tolerance of 1e-12.** They are deliberately not written the way
each gate prints them for a human, which is two decimal places for microcoulombs
and one for a percentage.

The form differs by language and that is allowed on purpose. The C and the C++
use `%.17g`, seventeen significant digits being enough to round-trip any double.
Rust has no `%.17g`, and its `{:?}` is the shortest representation that
round-trips, so it writes that instead. Both are read back by the test with
Python's `float()`, which is itself correctly rounded, so the comparison is
between the doubles the three computed and not between the strings they chose.
Requiring one spelling would have meant writing a float formatter in Rust to
imitate a C one, which is a worse thing to have in the repository than two
spellings and a sentence explaining them.

The reason is the same one P06 settled and is worth repeating here. Rounding a
double to two decimal places is a decimal conversion, and four languages do not
have to agree on the tie case: a value exactly halfway between two hundredths may
round to even in one and away from zero in another. Comparing those strings would
be an experiment about four decimal printers, and this project's claim is about
whether a charge regression turns a build red. So the arithmetic is compared at a
tolerance nine orders of magnitude tighter than the five percent the gate rules
on, and each implementation's human-readable prose stays its own business.

The verdict, the refusal, the failure codes, the names and every integer are
compared exactly. A gate that reached a different verdict, or named a different
phase, would be a real disagreement whatever the arithmetic did.

## Deliberately out of scope

**A duplicate name in a request.** The real gate reads JSON, where a repeated key
has already been resolved by the parser before the gate sees it, and the Python's
input is a dictionary that cannot hold one. A rule about duplicates would exist
only in the three implementations that parse the line protocol, so it would be a
property of this test harness rather than of the gate, and the parity test could
not compare it against anything.

**Names are ASCII.** The gate sorts by name, Python sorts strings by code point
and C compares bytes, and those agree for ASCII and need not for anything else.
Every target and phase name in this repository is ASCII, and a name that was not
would be a question about the build system rather than about the gate.
