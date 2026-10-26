# P18 in Rust

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine, which
has no `cargo` and will not get one; the first toolchain to see it is the one
`rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI, which runs `cargo fmt --all --check`, `cargo clippy
--workspace --all-targets -- -D warnings` and `cargo test --workspace`.

| File | What it is |
|---|---|
| `src/lib.rs` | the cascade at both widths, the Q15 conversion, both metrics, four unit tests |
| `src/main.rs` | the same six verbs the C++ filter speaks, so one parity test drives both |
| `Cargo.toml` | no dependencies, and the comment says what that costs here |

## Three languages, three answers to one duplication

The cascade is the same code at two floating-point widths, and this project now
holds three different answers to that, which is worth recording because it is one
problem three times:

| Language | How the two widths are written |
|---|---|
| C | **two functions**, `biquad_df2t_f64` and `biquad_df2t_f32`, forty lines each and identical but for the type of five locals |
| C++ | **one function template**, instantiated at `double` and at `float` |
| Rust | **one `macro_rules!` body**, because `core` has no numeric tower |

There is no trait in Rust's standard library that `f32` and `f64` share for
multiply and add. A cascade generic over the width needs either the `num-traits`
crate or a trait defined here, and this crate has no dependencies by the rule the
other three follow. So the two functions come from one macro: one source of
truth, like the C++, reached by a different route and with worse diagnostics when
it goes wrong.

**That is a cost of the language in a direction people do not usually expect**,
and it is the second one this volume has found. The first is immediately below.

## This crate is std, and that is the same finding P06 recorded

`accept_metrics` needs `sqrt` and `log10`. Both live in `std` and not in `core`,
because `core` assumes no platform and those functions live in the platform's
libm. A `no_std` version would need the `libm` crate.

P06's witness is `std` for exactly that reason, and its note says the C compiles
for the target unchanged because newlib provides `sqrt`. The same is true here:
`../c/dspaccept.c` would cross-compile as it stands, and this crate would not
without a dependency. Two projects now say it, which makes it a property of the
language on this part rather than an anecdote about one file.

`code.yml`'s step that compiles every `no_std` library for the board's target
names its crates explicitly rather than globbing, so this one is simply absent
from that list, as `p06-rate` is, and the comment there says why.

## What the unit tests cover, and what they do not

Four tests that need no oracle file and run under `cargo test` alone: a pure gain
exact in both widths, the six refusals, two identical blocks reporting exact
rather than infinite decibels, and the floor shift's rounding direction on
negative values. That last one is deliberate: it is the single line that decides
every negative sample in the fixed-point path, and the three languages reach it by
three routes, a division and a remainder test in the C and the C++ and
`div_euclid` here.

**The oracle itself is driven through the filter by
`python/tests/test_dspaccept_parity.py`**, not duplicated here, because the oracle
is the project's property and not any one language's. Every test in that file that
drives a filter now runs twice, once for the C++ and once for this, with the
language in the test id so a failure names which one disagreed.

## What is not here

What is not in the C either: the real transform and its packed output, the complex
magnitude, the cycle counter and the eight-case placement grid. Those need the
part. There is no `no_std` build of this crate and no target build, for the reason
above.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
