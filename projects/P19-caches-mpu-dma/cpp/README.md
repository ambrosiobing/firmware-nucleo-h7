# P19 in C++

**State: host, since Friday 30 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `mpualign.hpp` | header only: the range rounding, criteria 4 and 5 |
| `mpualign_filter.cpp` | three verbs on stdin, so one parity test drives this beside the Rust |

The four findings this project is about are in
[../c/mpualign.h](../c/mpualign.h) and are not repeated here.

**This is the last of eighty implementations**, four languages across twenty
projects, and the question it answers is the one asked of every C++ variant in
the volume: what does the language add. The answer here is **almost nothing, and
the reason is nameable**, which makes it the most useful entry in that list
rather than the emptiest.

## Where the Rust had something and this has not

Finding four is that the aligned end can need the value 2 to the 32nd, which a
`uint32_t` cannot hold, and that storing zero there is the failure rather than an
approximation of it. The Rust asks the 32 bit type:

```rust
let last = addr.checked_add(len).ok_or(Error::Wrap)?;
let end = last.checked_next_multiple_of(line).ok_or(Error::Wrap)?;
```

C++17 has **no checked arithmetic and no `next_multiple_of`**. `std::popcount`
and `std::bit_width` arrived in C++20 with `<bit>`, and the compiler builtins
`__builtin_add_overflow` and `__builtin_mul_overflow` are a gcc extension this
volume does not rely on. So this file widens to 64 bits exactly as the C does,
and the region logarithm counts shifts exactly as the C does.

**Both places where the Rust's standard library expressed the question better
are places C++17's does not**, so on this project the C++ is the C with a
`std::optional` and a namespace.

P17 reached the same conclusion from a different direction, four small functions
over fixed-width integers, and two instances make it a property of the problem
rather than a remark about one file: **when the arithmetic is already in the
narrowest type that fits, there is nothing for a richer language to carry.**

What it does add is the one thing it has added in nine of the twelve entries
before it: `std::optional<Error>` makes a refusal a different type rather than a
negative return code, so an answer cannot be read without the question being
answered first.

## What is identical

The order of the six refusals and their names; the power-of-two test on the
line, because the rounding is a mask rather than a division; a buffer of no
bytes refused rather than answered with an empty range; the 64 bit intermediate
and the comparison against 2 to the 32nd, which is finding four; the two shared
lines reported **separately**, which is finding one; criterion 4 answered as one
verdict, with a shared line in either position being a failure of alignment;
the region base checked against its **own** size, which is finding three; and
the absent upper bound on the region size, with the C's reason repeated, because
a guard that can never fire reads as coverage.

`Error::kArgument` is never returned here, because the answers go into
references rather than through pointers that could be null. The name is kept so
the six refusals mean the same thing in all four implementations.

## One line in the filter is about two laptops

The numbers are read with `strtoull` rather than `strtoul`, and the comment says
why: **`unsigned long` is 32 bits under win11 aquamarine's compiler and 64 bits
under skyhorizon's**, so an input of 4294967296 would overflow on one laptop and
not the other, and the two would disagree about whether to refuse it. The wider
type makes the explicit bound the only check, and that check is the same
everywhere. The top of the address space is exactly what finding four is about,
so a filter that could not carry those values could not test it.

## The filter protocol

```text
M <addr> <len> <line>   -> base=.. end=.. bytes=.. lines=.. head=0|1 tail=0|1
B <addr> <len> <line>   -> ok=1
R <base> <size>         -> field=N
```

Every value is decimal, addresses included, which is P01's decision for register
words applied again. `B` answers `ok=1` or a refusal and never `ok=0`, because
criterion 4 is a build time assertion and a flag would invite a caller to read
the flag instead of the refusal.

Every value is an unsigned integer, so this filter and the Rust print the same
text for the same answer. One test function drives both for each of the two
things they are asked: every case in the oracle's order, and finding four's
boundary in four requests.

## What is not here

The transfer engine, the stale read reproduced across a thousand runs, the three
fixes and their failure rates, the two cycle costs per transfer, the reachability
table against RM0455, and the fourth failure with its register evidence. Those
need the board, a sensor on a breadboard and RM0455. There is no measured number
in this file; the one measured figure the header quotes, the 32 byte line, came
from this project's own run on the board on Saturday 3 October 2026.
