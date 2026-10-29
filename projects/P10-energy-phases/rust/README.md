# P10 in Rust

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std` with `f64`: the ledger, the runs, the residual, six unit tests |
| `src/main.rs` | one verb on stdin, so the parity test drives it beside the C and the Python |
| `Cargo.toml` | no dependencies, and why the magnitude is written out |

The reasoning this project is built on is in
[../c/phaseledger.h](../c/phaseledger.h) and is not repeated here, including the
two that matter most: **the total is integrated with no reference to the markers
at all**, because a residual computed between the phase sum and itself would
always be zero and prove nothing, and the runs are cut by **index** rather than
by time, which is the whole correction this project makes to the chapter's
sketch.

## The residual is the acceptance test, and it is meant to be non-zero

A run shorter than the floor is rejected and the charge drawn inside it stays in
the total while leaving the split, so the residual is non-zero **by
construction** whenever anything was rejected. `rejected_samples` is what makes
that checkable rather than a guess.

A reader who expects the residual to be zero has mistaken a measurement for a
checksum, and a test here states the arithmetic rather than the expectation: one
sample of five rejected gives a residual of exactly one fifth.

## `f64::abs` is in std, so the magnitude is written out, for the second time

This crate is `no_std`, and `f64::abs` lives in std for the same reason `sqrt`
and `log10` do. So the magnitude is a comparison, which is what the C writes
too, by choice rather than by need:

```rust
if v < 0.0 { -v } else { v }
```

**P12's crate was the first to meet this**, where the obvious spelling of its
charge reconciliation needed `abs` and was written as two comparisons instead so
the crate could stay `no_std`. Two crates now say it, which makes it a property
of the split between `core` and `std` rather than an anecdote about one file.

The volume's running list of what that split costs on this part:

| | |
|---|---|
| P06, P18 | `sqrt` and `log10` are std, so those two crates are std |
| P12, P10 | `abs` is std, so both write the magnitude as a comparison and stay `no_std` |
| P07, P17 | `+ - * /` and comparison are core, so `f64` arithmetic alone needs no std |

## What is identical

The total integrated before the markers are looked at; the accumulate-then-scale
grouping, which the oracle compares exactly; the runs cut by index; a code that
recurs accumulating into the same slot rather than replacing it; the unused
codes 5 and 6 carried rather than refused, because a trace containing one is a
finding; the floor rejecting a short run and leaving its charge in the total;
a total of zero answering a residual of one rather than dividing; and the four
refusals with their names.

Not identical: `Error::Argument` is the C's null pointer refusal and here means
only an empty trace, because a slice cannot be null. The name is kept so the
four refusals mean the same thing in all three implementations.

One predicate is spelled differently and is exactly the same. The C guards the
interval with `!(dt_s > 0.0)`; clippy rejects that shape as
`neg_cmp_op_on_partial_ord`, so this writes `x.is_nan() || x <= 0.0`, which
refuses the same values and accepts an infinite interval as the C does. A test
asserts both halves.

## The filter protocol

One verb, because this project is one function.

```text
L <dt_s> <min_samples> <n> <current code>*n
    -> total=.. split=.. residual=.. runs=.. rejected=.. rejected_samples=..
       per0=.. per1=.. .. per7=..
```

**All eight per-code charges are printed, zeros included.** Printing only the
non-zero ones would hide exactly the case the unused codes exist to show.

Every floating point value crosses the protocol exactly, parsed with
`str::parse` and printed with `{:?}`, and a test checks that the 49 floating
point tokens in the 13 requests read back as the values written **with no filter
and no compiler**, so that property is verified on the authoring laptop too.

The residual is the one figure the C and the Python compare with a tolerance,
`abs=1e-15`, because it is a quotient rather than a sum of powers of two. The
Rust is held to that same bound and not a tighter one, so the three agree on
what is being claimed.

## What is not here

The PPK2, the trace it records, the marker pins and every measured charge. Those
need the probe and the board. There is no measured number in this crate.

It does compile for the board's target: the guard added earlier today, which
compares the cross step's crate list against every `no_std` crate, **refused
this commit until this crate was added to that step**, which is what it was
written for.
