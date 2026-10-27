# P17 in Rust

**State: host, since Tuesday 27 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI, which runs `cargo fmt --all --check`, clippy with
warnings as errors, and the tests.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std`: elapsed ticks, the wrap period, the subtraction, the report, seven unit tests |
| `src/main.rs` | four verbs on stdin, so one parity test drives this beside the C and the Python |
| `Cargo.toml` | no dependencies, and why this crate is `no_std` where two others are not |

## What this language says about the chapter's own defect

Step 6 subtracts the calibrated cost of reading the counter with
`(b - a) - cyc_overhead` on a `uint32_t`. The same expression in Rust behaves
differently, and **the difference is narrower than the usual claim about this
language**:

| | An interval of 5 ticks against a calibrated cost of 10 |
|---|---|
| C, every build | 4294967291 ticks, silently |
| Rust under `cargo test` or any debug build | **panics**, "attempt to subtract with overflow" |
| Rust in this workspace's release profile | 4294967291 ticks, silently |

Cargo's dev profile takes `overflow-checks` from `debug-assertions`, which is on,
and the root manifest does not override it. Its `[profile.release]` sets
`opt-level`, `lto`, `codegen-units`, `panic` and `debug`, and **not**
`overflow-checks`, so release leaves it off.

**So the honest statement is that Rust's arithmetic checks would have caught this
during development and not in the field.** That is a smaller claim than "this
cannot happen in Rust", and it is the only one the two profiles support. A test
below asserts the panic with `should_panic`, and a second shows the wrapped value
through `wrapping_sub`, which is profile independent.

**And the fix was available in every language.** This crate uses `checked_sub`;
the C refuses on the same condition with an explicit comparison, which is three
lines. The language did not make the fix possible. It made the unfixed version
noisy in one profile out of two.

## This crate is no_std, where two others are not

P06's witness and P18's acceptance metrics are `std` because `f64::sqrt` and
`f64::log10` live there and not in `core`. This one divides by a frequency, and
`f64` division is in `core`, so it needs nothing. **That makes three data points
rather than two anecdotes**: what pushes a crate in this volume to `std` is
transcendental and root functions, not floating point itself.

One consequence is in the tests. The assertion that the wrong answer is the size
of one whole wrap is written as two comparisons rather than with `f64::abs`,
because `abs` is `std` for the same reason `sqrt` is. P12's gate made exactly
that substitution to stay `no_std` and its README records it.

**It is not yet in `code.yml`'s step that compiles every `no_std` library for the
board's target**, which names its crates explicitly rather than globbing. Adding
it there is a change to a workflow step, and a workflow step is answered only by
the runner, so it waits on evidence from one rather than on a clean build here.

## What it is proven against

The same oracle as the C and the Python, driven through the filter: 15 cases and
13 refusals, compared exactly. Plus seven unit tests that need no oracle file and
run under `cargo test` alone, including the panic above and the chapter's three
wrap periods.

## What is not here

The bus read in Rust, which is one of chapter 17's four implementations and needs
the peripheral, the shield and the hardware crate's peripheral module routing.
That routing is verified in the chapter and is not this file's subject.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
