# P04 in Rust

**State: host, since Wednesday 28 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI, which runs `cargo fmt --all --check`, clippy with
warnings as errors, and the tests.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std`: the span split, the advance, the two guards, six unit tests |
| `src/main.rs` | one verb on stdin, so the parity test drives it beside the C and the Python |
| `Cargo.toml` | no dependencies, and why this crate is `no_std` where two others are not |

The reasoning this project is built on is in
[../c/rxspan.h](../c/rxspan.h) and is not repeated here, including the one that
matters most: **the guard is not a detector**. If the consumer falls behind far
enough the engine overwrites bytes nobody read, and this arithmetic cannot tell
a full lap from no movement, because both arrive as `pos == old_pos`.

## What this language adds: the count and the array cannot disagree

The C returns an `int` of 0, 1 or 2 and writes that many entries into
`out[RXSPAN_MAX]`. **Those two can disagree.** A caller who reads `out[1]` when
the count is 1 gets whatever was in that slot before, and nothing in the
signature prevents it: the convention is that the return value says how far to
read.

Here the answer is a `Delivery`, which is `Nothing`, `One(span)` or
`Two(span, span)`. There is no count to disagree with anything and no slot to
read that is not there. The count is derived from the shape when the protocol
needs it rather than stored beside it.

**That is the same move the C++ makes in P20**, where each percentile is a
`std::optional` instead of a value beside a flag that can contradict it. Two
instances, in two languages, on two projects, and both are the same
observation: where the C carries a convention, a sum type carries a guarantee.

What it is not: a better algorithm, fewer lines, or something the C could not
express with a tagged union. It removes one specific way for a caller to be
wrong, and the case where that matters is in the oracle, because wrapping
exactly onto zero is **one** span and not two.

## The deliberate wrap, which is the other side of a story told twice

The advance is `(pos - old_pos)` masked, and on a `u16` that subtraction
underflows whenever the buffer has wrapped, which is the normal case this
function exists for. In Rust that panics in debug and in test, so the wrap has
to be spelled `wrapping_sub`.

This volume has now met Rust's overflow checks three times:

| | |
|---|---|
| P17, the instrument subtraction | the wrap was a **defect**, and the check would have caught it in development |
| P20, the percentile rank product | the wrap was a **defect**, and the check would have caught it in development |
| P04, here | the wrap is **correct and intended**, and the check forces it to be named |

Two of three were defects, which is why those two READMEs say what they say.
The third is the useful generalisation: **the checks do not find wraps, they
force the author to declare which wraps are meant.** A `wrapping_sub` in the
source is a statement a reviewer can disagree with, where the C's silence is
not. A test here asserts the intermediate, 65046 for a position of 10 against
an old position of 500, so the wrap is a measured step and not a hope.

## What is identical

The power-of-two refusal, which is load bearing because the advance is a mask;
the floor of eight, below which half a buffer stops meaning anything; the
position refused at or past the end; the advance computed by masking rather
than by subtracting; the near-lap boundary at strictly more than half, so half
exactly does not count; the tail-then-head order of a wrapped pair; wrapping
exactly onto zero giving one span; and the statistics updated on every call
that is not refused, including calls that find nothing, because the count of
calls is what the chapter's half and full transfer interrupts exist to keep
above a floor.

Not identical: `Error::Argument` is the C's null pointer refusal and is
unreachable here, because a `&mut` cannot be null. It is kept so the three
names mean the same thing in all four implementations and a filter answer never
has to be translated.

## The filter protocol

One verb, because this project is one function.

```text
N <size> <old_pos> <pos>
    -> spans=n [off0=.. len0=.. [off1=.. len1=..]] peak=.. near_lap=.. calls=.. deliveries=..
```

Each request runs on fresh statistics, which is how the oracle states its
cases: one call, one answer, so the peak and the near-lap count describe that
call rather than the run. A refusal is `REFUSED` and one of `argument`, `size`
or `position`, and a request with anything left over is refused.

**The parity test asserts that a span index the case does not have is absent
from the answer**, not merely that it is ignored, which is the one thing the
sum type guarantees and the C's count beside an array does not.

## What is not here

The counter read itself, the three interrupt handlers that all call one
function, and the cache invalidate that the canonical reference leaves out.
Those are chapter 4's remaining work and need the board. There is no measured
number in this crate.

**It does compile for the board's target**, since Thursday 29 October 2026, when
`code.yml`'s cross step was given the six `no_std` crates it was missing under a
name that said every. Compiling for `thumbv7em-none-eabihf` is not running on
the board, and it is the half that can be checked without one.
