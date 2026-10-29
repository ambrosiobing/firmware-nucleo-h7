# P04 in C++

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `rxspan.hpp` | header only: the span split, the advance, the two guards |
| `rxspan_filter.cpp` | one verb on stdin, so one parity test drives this beside the Rust |

The reasoning this project is built on is in [../c/rxspan.h](../c/rxspan.h) and
is not repeated here, including the one that matters most: **the guard is not a
detector**, because a full lap and no movement both arrive as
`pos == old_pos`.

## What C++ adds here, which makes six answers on record

The answer varies by project and keeping it accurate is the point of asking:

| | What C++ adds |
|---|---|
| P18 | one function template where the C needs two forty line copies |
| P12 | the C's lack of an allocator costs it sixty lines of fixed buffers, bounds checks and an insertion sort |
| P17 | almost nothing: four small functions over fixed-width integers |
| P16 | a fourth independent algorithm for the one quantity where implementations have real room to disagree |
| P20 | `std::optional` turns finding three from a convention into a type |
| P04 | `std::variant` reaches the **same** guarantee the Rust gets from an enum, and charges more for it |

## The guarantee, and what it costs in this language

The C returns an `int` of 0, 1 or 2 and writes that many entries into
`out[RXSPAN_MAX]`. **Those two can disagree**, and a caller who reads `out[1]`
when the count is 1 gets whatever was in that slot before.

`std::variant<std::monostate, Span, Pair>` is the same sum type the Rust gets
from `enum Delivery`, exactly: no count to disagree with, and no slot to read
that is not there. **`std::visit` over it is checked for exhaustiveness at
compile time** the way a Rust `match` is, because an overload set missing an
alternative does not build.

So the guarantee is equal. What is not equal is the ceremony:

| | Consuming the answer |
|---|---|
| Rust | one `match`, three arms |
| C++ | `std::visit` with an overload set, which here is a generic lambda plus `if constexpr` on `std::decay_t<decltype(held)>`, about five lines of scaffolding before any of the answer is written |
| C++, the shortcut | a sequence of `std::get_if` checks, shorter, and **exhaustive only by the author's discipline**, which hands back the thing the variant was for |

This file uses `std::visit` at both places that consume the answer, the count
and the text, and `std::get_if` nowhere. A guarantee kept in one place and given
up in another is worth less than either.

**That is the honest shape of the comparison.** Saying C++ cannot express it
would be wrong. Saying the two are equivalent would skip the cost. It can, the
compiler verifies it, and it is longer.

## One difference that belongs to the Rust and not to this file

The advance is `(pos - old_pos)` masked, and that subtraction underflows
whenever the buffer has wrapped, which is the normal case this function exists
for. C and C++ define unsigned wraparound and say nothing about it; Rust's
checks stop a plain subtraction and force `wrapping_sub`.

[../rust/README.md](../rust/README.md) carries the table for that, because it is
the third time this volume has met those checks and the first where the wrap is
correct rather than a defect. Here it is one `uint32_t` expression with a
comment, which is exactly the point being made there.

## What is identical

The power-of-two refusal, load bearing because the advance is a mask; the floor
of eight; the position refused at or past the end; the advance masked rather
than subtracted; the near-lap boundary at strictly more than half, so half
exactly does not count; the tail-then-head order of a wrapped pair; wrapping
exactly onto zero giving one span; and the statistics updated on every call that
is not refused, including calls that find nothing.

Not identical: `Error::kArgument` is the C's null pointer refusal and is
unreachable here, because the two out parameters are references. It is kept so
the three names mean the same thing in all four implementations.

## The filter protocol

One verb, because this project is one function. The same one
`../rust/src/main.rs` speaks.

```text
N <size> <old_pos> <pos>
    -> spans=n [off0=.. len0=.. [off1=.. len1=..]] peak=.. near_lap=.. calls=.. deliveries=..
```

Each request runs on fresh statistics, which is how the oracle states its cases.
A refusal is `REFUSED` and one of `argument`, `size` or `position`, and a
request with anything left over is refused. **The parity test asserts that a
span index the case does not have is absent from the answer**, not merely
ignored, which is the one thing both sum types guarantee and the C's count
beside an array does not.

## What is not here

The counter read itself, the three interrupt handlers that all call one
function, and the cache invalidate that the canonical reference leaves out.
Those are chapter 4's remaining work and need the board. There is no measured
number in this file, and no `-ffp-contract=off` either, because every quantity
is an integer; since Wednesday 28 October 2026 the guard that enforces that flag
reads a header as well as its source, so the absence is checked rather than
asserted.
