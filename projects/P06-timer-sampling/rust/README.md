# P06 in Rust

**State: host, since Sunday 4 October 2026.** Written on win11 aquamarine, which
has no cargo. The first compiler to see it was cargo 1.99.0 in WSL on the win11
skyhorizon demo laptop, bing@JPTOUPM678, and it refused the crate on one lint,
which is recorded below rather than quietly fixed. Clippy with warnings as errors
is now clean, its seven unit properties pass, and the parity table lists four
languages and not three. The commands, on that laptop:

    cargo clippy --workspace --all-targets -- -D warnings
    cargo test --workspace
    cargo build --release --workspace
    python3 -m pytest python/tests/test_rate_parity.py -q -s

| File | What it is |
|---|---|
| `Cargo.toml` | the crate `p06-rate`, no dependencies, a workspace member |
| `src/lib.rs` | the witness |
| `src/main.rs` | the filter `p06-filter`, the one verb the C++ filter speaks |

## This is the one crate in the workspace that is not `no_std`, and that is the finding

`f64::sqrt`, `f64::abs` and `f64::is_nan` live in `std` and not in `core`.
Rust's core library has no floating-point maths at all, because those functions
live in the platform's libm and `core` assumes no platform. A `no_std` version of
this crate would need the `libm` crate as a dependency.

So the choice was between a dependency added to preserve a pattern and an honest
exception. The witness reads a recording an instrument produced and decides
whether a rate claim stands; it has no reason to run on the board. The other five
crates are `no_std` because they are code the board will link. This one is not.

**The C is different, and the difference is the part worth printing.** `rate.c`
compiles unchanged for the target, because newlib provides `sqrt`. The same
program is portable to this part in C and is not in Rust without a crate. That is
a real cost of the language on this part, in the one direction people do not
usually expect, and `code.yml` therefore names the five `no_std` packages for the
`thumbv7em-none-eabihf` build rather than building the workspace.

## One version bump, and why it was worth it

This crate declares `rust-version = "1.77"` where the other five declare 1.75.
`f64::round_ties_even` was stabilised there, and it is what makes the
missing-edge count agree with Python's `round` and C's `nearbyint`, all three of
which round half to even. Rust's `f64::round` rounds half away from zero.

The three would disagree only when an interval is exactly a half multiple of
nominal, which a real recording never produces and a synthetic one easily could,
and a parity test over synthetic captures is precisely where that would surface.
Hand-rolling the tie rule to save a version would have put a rounding rule of my
own in the one file whose job is agreeing with three other implementations.

`cargo test` carries seven properties that need no other implementation,
including the clean wave at the wrong rate failing on the rate and not on
jitter, a flat recording refused rather than called perfect, and a perfect
straight line fitted with no residual.

## A lint refused by name, and why that is the right answer here

`cargo clippy -D warnings` in WSL on bing@JPTOUPM678 refused this crate on
Sunday 4 October 2026, and it was the only one of the six it refused:

    error: this comparison involving the minimum or maximum element for this
    type contains a case that is always true or always false
       --> projects/P06-timer-sampling/rust/src/lib.rs
        |
        | out.no_missing_edges = out.missing_edges <= MAX_MISSING_EDGES;
        |
        = help: consider using `out.missing_edges == MAX_MISSING_EDGES` instead

Clippy is correct. `MAX_MISSING_EDGES` is 0 and `missing_edges` is a `usize`, so
`<=` can only ever be equality and the `<` half of it is unreachable.

**The suggestion was still refused, and the reason is worth more than the lint.**
Criterion 4 is a budget: at most `MAX_MISSING_EDGES` missing edges. Writing
`== MAX_MISSING_EDGES` inverts that criterion the moment the budget stops being
zero, because a budget of one would then report a capture with no missing edges
at all as a failure. The comparison that survives a change to the constant is the
ordering one, which is what all four implementations write, so the lint is
silenced by name at that one statement with the reason beside it.

This is the third finding about Rust on this part, and unlike the other two it is
not about the language's capability. A linter that is right about the code as it
stands can still propose a change that is wrong about the code as it is meant to
be read, and a budget constant set to the minimum of its type is exactly where
that happens. The C, C++ and Python write the same comparison and have no
equivalent lint to answer, which is why this one was found in Rust and would have
been found nowhere else.

## What it is proven against

Nine synthetic captures whose answers are known by construction, written to
files because a capture is thousands of doubles, and driven through every
implementation by `python/tests/test_rate_parity.py`:

| Capture | What every implementation must say |
|---|---|
| a clean kilohertz square wave | pass, and 1000.014 Hz from the fit |
| **a clean wave at 1100 Hz** | **fail on the rate, pass on the jitter.** This is the one the project exists for: a clean wave at the wrong rate, which a witness that called it a pass would be worthless for |
| jitter of two microseconds | pass, standard deviation 7.4 microseconds against the ten the criterion allows |
| jitter of thirty microseconds | fail on jitter, pass on the rate, because the mean period is unchanged |
| one dropped edge | one missing edge, from an interval near twice nominal |
| one doubled edge | two missing edges, from two intervals near a half |
| offset and attenuated | identical to the clean capture, which is what proves the thresholds come from the observed swing and not from assumed rail voltages |
| a flat recording | refused for having no swing, rather than reported as perfect |
| a fragment | refused for having too few edges |

**Why this comparison is numeric where the other four are exact.** The other
parity tests compare bytes, row indices or integers and demand equality. This one
compares doubles to a relative tolerance of 1e-12. Double arithmetic is
deterministic, but the contraction of a multiply and an add into one fused
instruction is not the same across four toolchains: gcc may contract where rustc
does not, and the Cortex-M7's floating point unit has a fused multiply-add where
a host may not. The C and C++ are compiled with `-ffp-contract=off` to remove
the question where it can be removed, and the tolerance covers what remains. The
pass criteria are 0.1 percent, so 1e-12 is nine orders of magnitude tighter than
anything the measurement claims.

The booleans and the counts are compared exactly. A verdict that differed between
implementations would be a real disagreement whatever the arithmetic did.

## What no host can compare

Whether the board actually samples at 1 kHz. That is what the witness is for, and
it needs the board, the marker pin and the MCC 118. The three acquisition back
ends still refuse at run time rather than guessing a converter, timer or transfer
engine setting RM0455 governs.
