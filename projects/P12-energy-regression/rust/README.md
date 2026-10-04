# P12 in Rust

**State: host, since Sunday 4 October 2026.** Written on win11 aquamarine, which
has no cargo. Built and proven in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678:

    cargo clippy --workspace --all-targets -- -D warnings
    cargo test --workspace
    cargo build --release --workspace
    python3 -m pytest python/tests/test_gates_parity.py -q -s

| File | What it is |
|---|---|
| `Cargo.toml` | the crate `p12-gates`, no dependencies, a workspace member |
| `src/lib.rs` | the two gates |
| `src/main.rs` | the filter `p12-filter`, the two verbs the C++ filter speaks |

## It came within one function call of P06's fate, and that is the finding

P06's rate witness is the one crate in this workspace that is not `no_std`,
because `f64::sqrt` lives in `std` and not in `core`: Rust's core library has no
floating-point maths at all, since those functions live in the platform's libm
and `core` assumes no platform.

This gate needs one piece of floating-point maths, the absolute difference
between a ledger's reported total and the sum of its phases, and `f64::abs` is in
`std` for exactly the same reason `sqrt` is. One dependency, for one call.

It is written as two comparisons instead:

    if summed - total > slack || total - summed > slack

which needs nothing, and behaves identically on a NaN: `abs(NaN) > slack` is
false, and so are both of these. So this crate is `no_std` and
`cargo build --target thumbv7em-none-eabihf --lib` includes it where it excludes
P06.

**Whether that was worth doing is a fair question, and the answer is yes for a
reason that is not about this crate.** The two comparisons are slightly less
direct than one `abs` call. What they buy is a second data point on the same
question, and two data points make it a pattern rather than an anecdote: `core`
has no floating-point maths, that bites any numeric code written for a target in
Rust, and whether it costs you a dependency comes down to whether your particular
arithmetic can be spelled without libm. P06's could not. This one could, barely.

## What it does need is `alloc`

The answer is a string of unknown length built from a map of unknown size, and
that is allocation however it is spelled. The C avoids it with fixed buffers and
a refusal when they do not fit, which is the right shape for a file that compiles
for the target and costs the C about sixty lines of appending and bounds checking
this file does not have.

Neither is better. They are the same rules written for different machines, and a
`--lib` build for the board's target needs no allocator, which is why CI can
include this crate at all. A binary would need a `#[global_allocator]`, and
nothing in this repository has one.

`cargo test` carries twelve properties that need no other implementation,
including the chapter's criterion, the boundary from both sides, a missing phase,
the refusal order, and that the answer does not depend on the order the case
arrived in.

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
