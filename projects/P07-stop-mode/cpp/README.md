# P07 in C++

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `cyclecharge.hpp` | header only: the segmentation, the report, the battery arithmetic |
| `cyclecharge_filter.cpp` | three verbs on stdin, so one parity test drives this beside the Rust |

The reasoning this project is built on is in
[../c/cyclecharge.h](../c/cyclecharge.h) and is not repeated here, including
finding one: a trace that begins with the marker already high has no rising edge
in front of its first cycle, so that cycle is not complete and is not reported.

## What C++ adds here is P12's answer a second time

The volume's answers so far, kept accurate because the question is only worth
asking if it can come back different:

| | What C++ adds |
|---|---|
| P18 | one function template where the C needs two forty line copies |
| P12 | the C's lack of an allocator costs it sixty lines of fixed buffers, bounds checks and an insertion sort |
| P17 | almost nothing: four small functions over fixed-width integers |
| P16 | a fourth independent algorithm for the one quantity where implementations have real room to disagree |
| P20 | `std::optional` turns finding three from a convention into a type |
| P04 | `std::variant` reaches the same guarantee the Rust gets from an enum, and charges more for it |
| P07 | **an allocator, again**: a parameter pair and a refusal disappear |

**The parameter pair.** The C's per-cycle charge takes
`double *out_uc, uint32_t max_out, uint32_t *count`, three parameters for one
answer, because the caller owns the buffer. Here the answer is a
`std::vector<double>` the function fills and the count is its size.

**The refusal.** `CYC_ERR_ROOM` exists in the C only because the caller might
not have supplied enough space, so **it is unreachable in this implementation's
`per_cycle_charge`**. The enum value stays, so the seven refusal names mean the
same thing in all four implementations, and `report` still uses it for a
different reason: a count above the bound is a mistake about the data rather
than about a buffer.

This is not a new observation, it is P12's. One instance was a remark about one
gate; two make it the thing C++ reliably buys on this part. The pattern is the
same both times: **the refusal that disappears was never about the problem, it
was about the C's inability to return a variable amount.**

## Every comparison here is exact, which governs the file

The oracle's values are powers of two and the parity test compares `double` with
`==`, with no tolerance anywhere, so the order of operations is part of the
specification. Each of these is the C's, mirrored rather than rederived:

| | |
|---|---|
| a cycle's charge | the samples accumulated and **then** divided, `sum / fs_hz` |
| the spread | `100.0 * (max - min) / median`, in that grouping |
| the cell energy | `cell_mah * 1e-3 * 3600.0 * cell_volts`, left to right |
| an even median | `0.5 * (a + b)` |

**One algorithm is not mirrored, and that is stated rather than hidden.** The C
writes an insertion sort and this calls `std::sort`. The two produce the same
sorted sequence for any set of numbers, and only the order matters to everything
downstream, so the exact comparison is unaffected. It would be affected by a
value that is not a number, which no oracle case holds and which the C's own
insertion sort orders arbitrarily too. The Rust twin writes the insertion sort
out for a different reason, which its README gives.

## And it still needs a build flag the Rust does not

The build passes `-ffp-contract=off`, because gcc at `-O2` fuses a multiply
followed by an add, rounding once where two operations round twice, and this
file is compared bit for bit against twins that cannot fuse.
`python/tests/test_float_builds_disable_contraction.py` enforces that, and since
Wednesday 28 October 2026 it reads a header as well as its source, which is why
the flag here is a checked requirement rather than an author's habit.

[../rust/README.md](../rust/README.md) is about the other side of that: Rust
fuses only where an author writes `mul_add`, so there is no flag to pass and
none to forget. This file is the one that has to pass it.

## The filter protocol

Three verbs, one per function, the same three `../rust/src/main.rs` speaks.

```text
C <fs_hz> <cycle_bit> <max_out> <n> <current digital>*n   -> count=n uc=a,b,c
R <volts> <min_cycles> <count> <count charges>            -> cycles=.. median=.. ..
L <energy_uj> <cycles_per_day> <period_s> <sleep_ua> <sleep_v> <mah> <cell_v>
```

**`max_out` is read and ignored here**, and the filter says so at the point it
does it. The C needs it; this implementation has no buffer to overrun. The token
stays because one test drives four implementations and the request has to be the
same for all of them.

Every floating point value crosses the protocol exactly: `strtod` in, which is
correctly rounded, and `%.17g` out, which is enough decimal digits that every
`double` reads back as the same bits. The Rust uses `str::parse` and `{:?}`,
which is the shortest form with the same guarantee, so the two filters print
different text for the same value and the test compares numbers rather than
strings.

## What is not here

The PPK2, the trace it records, and every measured charge figure. Those need the
probe and the board. There is no measured number in this file: the battery
arithmetic takes every assumption as an argument, which is the chapter's own
framing, and only `energy_per_cycle_uj` is ever a measurement.
