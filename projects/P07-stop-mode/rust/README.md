# P07 in Rust

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std`: the cycle segmentation, the report, the battery arithmetic, six unit tests |
| `src/main.rs` | three verbs on stdin, one per function, so the parity test drives it beside the C and the Python |
| `Cargo.toml` | no dependencies, why this crate is `no_std` with `f64`, and why no contraction flag |

The reasoning this project is built on is in
[../c/cyclecharge.h](../c/cyclecharge.h) and is not repeated here, including
finding one: a trace that begins with the marker already high has no rising edge
in front of its first cycle, so that cycle is not complete and is not reported.

## Every comparison in this project is exact, which governs the whole crate

The oracle's values are powers of two and the parity test compares `f64` with
`==`, with no tolerance anywhere. **So this is not an implementation that agrees
closely. It has to agree bit for bit**, which makes the order of operations part
of the specification rather than a detail:

| | |
|---|---|
| a cycle's charge | the samples accumulated and **then** divided, `sum / fs_hz`, not each sample divided and summed |
| the spread | `100.0 * (max - min) / median`, in that grouping |
| the cell energy | `cell_mah * 1e-3 * 3600.0 * cell_volts`, left to right |
| an even median | `0.5 * (a + b)`, which is what the chapter's own library does |

## What this language adds here: there is no flag to get wrong

On Monday 26 October 2026 P18's cascade turned main red because gcc at `-O2`
fuses a multiply followed by an add into one instruction that rounds once where
the two separate operations round twice. Every floating-point host build in this
repository passes `-ffp-contract=off` for that reason; a test enforces it; and
that test could not see a header-only library until Wednesday 28 October 2026,
when P16's C++ forced it to read includes.

**Rust needs none of that, because contraction is opt in.** `a * b + c` compiles
to a multiply and an add. Fusing is `a.mul_add(b, c)` and nothing else does it.
There is no flag to pass, no flag to forget, and no guard to write that might
not see the file.

**That is narrower than it may look.** It does not say Rust's floating point is
more accurate: `mul_add` exists, it is the *more* accurate of the two, and the
reason this repository refuses it is comparison against a twin that cannot fuse,
not accuracy. What Rust removes is a build setting whose omission silently
changes results, which on this volume has cost one red commit and one guard that
needed extending.

## And `no_std` with floating point, which is the second instance

This crate is `no_std` and computes in `f64`. Every operation here is `+`, `-`,
`*`, `/` and a comparison, all of which are in core. `sqrt` and `log10` are not,
and that is what makes P06's and P18's crates std.

P17's manifest said that first on Tuesday 27 October 2026, and this is the second
project to reach it independently: one instance was an observation about one
crate and two make it a rule worth stating once.

## One predicate written differently, and exactly

The C guards with `!(fs_hz > 0.0)`, which refuses zero, negatives and any value
that is not a number, and accepts an infinite one. **clippy rejects that shape**
as `neg_cmp_op_on_partial_ord`, which cost a round trip on P17 on Tuesday 27
October 2026.

The spelling here is `x.is_nan() || x <= 0.0`, which is the same predicate and
not a near equivalent: for a value that is not a number the first term is true,
and for every other value `x <= 0.0` is exactly the negation of `x > 0.0`. **An
infinite rate is therefore accepted here as it is in the C**, deliberately,
because the oracle has no such case and inventing a refusal the C does not make
would be a divergence rather than an improvement. A test asserts both halves.

The sort is also written out rather than taken from the slice, so the comparison
is the C's `>` on every pair and no question of how a library orders values that
are not numbers can arise.

## The filter protocol

Three verbs, one per function, each taking its counts before its data.

```text
C <fs_hz> <cycle_bit> <max_out> <n> <current digital>*n   -> count=n uc=a,b,c
R <volts> <min_cycles> <count> <count charges>            -> cycles=.. median=.. ..
L <energy_uj> <cycles_per_day> <period_s> <sleep_ua> <sleep_v> <mah> <cell_v>
```

**Every floating point value crosses it exactly**, which matters more here than
in any other filter in this volume: a value losing one bit on the wire would
fail a comparison the arithmetic passed. Python writes with `repr` and reads
with `float`; Rust parses with `str::parse` and prints with `{:?}`. Both pairs
are shortest-round-trip guarantees rather than conveniences, and a test in
`python/tests/test_cyclecharge_parity.py` checks all 1168 floating point tokens
in the 27 requests read back as the values written, **with no filter and no
compiler**, so that property is verified on the authoring laptop too.

A refusal is `REFUSED` and one of `argument`, `rate`, `bit`, `room`, `cycles`,
`supply` or `day`, and a request with anything left over is refused.

## What is not here

The PPK2, the trace it records, and every measured charge figure. Those need the
probe and the board. There is no measured number in this crate: the battery
arithmetic takes every assumption as an argument, which is the chapter's own
framing, and only `energy_per_cycle_uj` is ever a measurement.
