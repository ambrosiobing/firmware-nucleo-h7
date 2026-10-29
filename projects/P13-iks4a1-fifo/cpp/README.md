# P13 in C++

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `fifodecode.hpp` | header only: the decode, the gap, the budget |
| `fifodecode_filter.cpp` | three verbs on stdin, so one parity test drives this beside the Rust |

The four findings this project is about are in
[../c/fifodecode.h](../c/fifodecode.h) and are not repeated here.

## What C++ adds here is what it added to P20 and to P11

| | What C++ adds |
|---|---|
| P18 | one function template where the C needs two forty line copies |
| P12 | the C's lack of an allocator costs it sixty lines of fixed buffers, bounds checks and an insertion sort |
| P17 | almost nothing: four small functions over fixed-width integers |
| P16 | a fourth independent algorithm for the one quantity where implementations have real room to disagree |
| P20 | `std::optional` turns finding three from a convention into a type |
| P04 | `std::variant` reaches the same guarantee the Rust gets from an enum |
| P07 | an allocator, again: a parameter pair and a refusal disappear |
| P10 | `std::variant`, again, on a promise the C's own header makes in prose |
| P11 | two C conventions become types: a sentinel return and a sentinel terminated array |
| P13 | **`std::optional` and a sized container, so both of P11's answers at once**, and this time the optional carries a finding rather than a refusal |

**Nine projects in, this list has stopped producing new answers**, and that is
worth saying plainly rather than reaching for a novelty. It now produces the
same three: a template where the C repeats itself, a container where the C
passes a pointer and a count, and an optional where the C uses a sentinel or a
flag. **That is a fact about the C idiom rather than about C++.** These chapters
are written in the style this part is programmed in, so the same three shapes
recur, and a list that kept finding new answers would be a list of an author
looking for them.

## Finding two in the type system

The C's carried state holds **two fields for one fact**:

```c
uint8_t expect_cnt;  /* carried between drains */
uint8_t started;     /* 0 until the first word is seen */
```

The flag exists **because** of finding two: the chapter has no `started` at all,
its expectation is a static beginning at zero, so the first word of the first
drain is measured against a zero nobody wrote and the logger opens by reporting
drops that never happened. Adding the flag is the right fix.

But the pair can disagree, and nothing in the type prevents it. Here the field
is `std::optional<std::uint8_t>`, an empty optional **is** "no word seen yet",
and the comparison cannot be written without taking the expectation out of the
option first. [../rust/README.md](../rust/README.md) reaches the same place with
`Option<u8>` and lists the four crates in this volume that now carry a
convention as a type.

## The container carries its length

The C takes `const uint8_t *buf` with a separate `words`, and its header says
"words is the buffer level already clamped by the caller", so a level larger
than the buffer reads past the end: **undefined behaviour rather than a wrong
answer**. Here the block is a `const std::vector<std::uint8_t> &` and a buffer
too short for the stated word count is `Error::kArgument`.

**No oracle case reaches that**, because the oracle's buffers are built to
match, so it is not a new case the C would have to answer; it is the same
refusal name used for a condition the C cannot detect. C++20 would spell the
argument `std::span`, and this is C++17, so it is a const reference to a vector.

## And it needs a build flag the Rust does not

The build passes `-ffp-contract=off`, because gcc at `-O2` fuses a multiply
followed by an add and the drain time is exactly that shape. **The oracle
compares these figures to a relative 1e-12 rather than exactly, so a fused
result would probably pass**, and "probably passes" is not a reason to let a
build differ from its twins.
`python/tests/test_float_builds_disable_contraction.py` requires the flag,
reading the header as well as the source since Wednesday 28 October 2026.

## What is identical

The gap taken in the counter's own modulus; the five refusals in the C's order
and with its names; the tag table bound and an unknown tag counted rather than
indexed; the first word establishing the sequence; the counter advanced as
`(cnt + 1)` masked; the buffer filling at the **sum** of the batching rates with
the chapter's one-rate form beside it; the bus model of nine bits a byte plus
two address bytes plus a start and a stop; and the undetectable gap reported as
`1 << cnt_bits`.

Two lines are written differently for reasons that are about the warning set
rather than the chapter. The stride is bound to a `std::size_t` once, because
multiplying a `size_t` by a `uint8_t` promotes the byte to `int` and converting
that back is what `-Wsign-conversion` refuses; and the source count is bound the
same way, because comparing a `size_t` with a `uint8_t` is a signed comparison
once the byte promotes.

## The filter protocol

```text
G <expect> <cnt> <cnt_bits>              -> gap=N
D <word_bytes> <tag_shift> <cnt_shift> <cnt_bits> <known_tags>
  <words> <n> <first byte>*n             -> words=.. unknown=.. drops=..
                                            gaps=.. tags=a,b,..  (32 counts)
B <watermark> <sources> <word_bytes> <cnt_bits> <bus_hz> <decode_us>
  <rate>*8                               -> entries=.. fill=.. chapter=..
                                            drain=.. ratio=.. gap=N ok=0|1
```

**The word count and the number of supplied bytes are separate tokens**, and one
oracle case is the whole reason: "more words than the landing buffer can hold"
passes a level of 1025 with no bytes at all, because that refusal is decided
before anything is read. A filter that cannot carry a case cannot test it.

**The filter pads the block itself**, one tag byte per word followed by
`word_bytes - 1` zeros, which is what the C's side of the same oracle does, and
**all thirty-two tag counts are printed**, zeros included.

Floating point values go out as `%.17g`, enough digits that every `double` reads
back as the same bits; the Rust prints the shortest form with the same
guarantee, so the two filters print different text for one value and the test
compares numbers. One test function drives both.

## What is not here

The bus shim, the identification registers, the batching and watermark
configuration, the interrupt route, the empty handler, the overrun flag, the
ten-minute run and the comparison against polling. Those need the
X-NUCLEO-IKS4A1 on the board, which the bench holds, and the I2C pin mapping
that [../README.md](../README.md) describes. There is no measured number in this
file.
