# P15 in C++

**State: host, since Friday 30 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `zonetrack.hpp` | header only: the four states, the window, the policy check |
| `zonetrack_filter.cpp` | one verb on stdin, so one parity test drives this beside the Rust |

The reasoning this project is built on is in
[../c/zonetrack.h](../c/zonetrack.h) and is not repeated here, including which
source the specification comes from: step 9 gives two thresholds and the
confirmation count, figure 15.4 gives those **and** a third threshold of 1500 mm
that the sketch omits, and the figure is the more complete of the two.

## What C++ adds is the Rust's answer, one step short

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
| P13 | `std::optional` and a sized container, both of P11's answers at once |
| P14 | the same two again, and no decision about the standard library |
| P15 | **an `enum class` and `-Wswitch` remove the default arm**, which is most of the Rust's answer and not all of it |

**The default arm.** The C's `switch` over the state ends with
`default: return ZONETRACK_ERR_ARGUMENT;`, because its state is a plain enum in
a struct and any `int` fits there. Here the state is an `enum class`, nothing in
the file produces a value outside the four, and `-Wswitch`, which this
repository compiles as an error through `-Wall`, refuses a `switch` that omits a
known enumerator. So the default arm is gone and a fifth state added later is a
**compile error at every switch** rather than a silent fall-through.

**And the step it is short of.** An `enum class` is still an enum with an
underlying type: a cast or a `memcpy` can put a fifth value in one, so the
invalid state remains **representable** and merely unproduced. The Rust's enum
cannot hold a fifth value at all, so there the refusal is unreachable rather
than unproduced, which is why `name()` here keeps its "invalid" return after the
switch and the Rust's `State::name` has no such arm.

That difference is small and it is real. This is the first entry in the list
where C++ gets most of the Rust's answer **and the remainder can be named**,
which is more useful than either a tie or a verdict.

## One line counts differently and the count is the same

The C counts the window's bits in a loop; the Rust calls `count_ones`. **This
keeps the C's loop**, because `std::popcount` is C++20 and this file is C++17,
and the alternative was a `__builtin_popcount` with a fallback branch that
nothing in this repository would ever compile. An untested branch in a header is
worse than a loop over at most sixteen bits. Only the count reaches the
comparison, so none of this is in the specification.

## What is identical

The four edges of the figure and the two the figure does not draw, which are
this volume's decisions and are recorded: approaching returns to clear if the
target leaves without confirming, and leaving returns to present if the target
comes back. The thresholds are **strict**, so a distance sitting exactly on 800
is not under it and never confirms. The window is cleared on every edge.
`kNoTarget` is 0xFFFF and is further away than any threshold. The state
numbering is 0, 1, 2, 3 in the order clear, approaching, present, leaving,
because a host decoder reads `[(b >> 5) & 3]` and that numbering is the wire
format, which is why the enumerators carry their values explicitly.

Two bounds are widened explicitly where the C writes a cast, for the same
reason: `-Wsign-compare` and `-Wsign-conversion` are errors here, and a `uint8_t`
compared against a `std::uint32_t` promotes to `int` first.

## The filter protocol

```text
T <far> <enter> <leave> <confirm_n> <confirm_m> <n> <distance>*n
    -> frames=N changes=N ready=0|1 states=clear,approaching,..
    -> REFUSED policy
```

**The policy travels in every request** and nothing is held between lines, so a
request that names no policy cannot be answered from a previous one. Six of the
nineteen oracle cases override the chapter's numbers.

**The whole sequence of states comes back**, not the final one, and the change
count is checked against the oracle's own states rather than a figure beside
them. Every value in this protocol is an integer, so this filter and the Rust
print the same text for the same answer, which is unusual in this volume and
worth noting: it is the floating point filters that have to be compared as
numbers.

One test function drives both filters for each of the two things they are asked:
every case in the oracle's order, and the chatter comparison that shows why
there are two thresholds rather than one.

## What is not here

The sensor, the bus, the validity filter of step 8, the occupancy reduction and
the confidence field of the status byte. The chapter gives no sigma limit, no
minimum distance, no set of trusted status codes, no bucket mapping and no
confidence definition, and inventing six constants to fill a struct would be
fiction: they are bench decisions by the chapter's own account. They arrive when
the X-NUCLEO-53L8A1 is read, and that shield is already stacked on the board.
There is no measured number in this file.
