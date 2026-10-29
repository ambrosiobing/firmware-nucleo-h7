# P10 in C++

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `phaseledger.hpp` | header only: the run decode, the two totals and the residual |
| `phaseledger_filter.cpp` | one verb on stdin, so one parity test drives this beside the Rust |

The reasoning this project is built on is in
[../c/phaseledger.h](../c/phaseledger.h) and is not repeated here, including the
two that matter most: **the total is integrated with no reference to the markers
at all**, because a residual computed between the phase sum and itself would
always be zero and prove nothing, and the runs are cut by **index** rather than
by time, which is the whole correction this project makes to the chapter's
sketch.

## What C++ adds here is P04's answer a second time

The volume's running list, kept accurate because the question is only worth
asking once per project if the answer can come back different:

| | What C++ adds |
|---|---|
| P18 | one function template where the C needs two forty line copies |
| P12 | the C's lack of an allocator costs it sixty lines of fixed buffers, bounds checks and an insertion sort |
| P17 | almost nothing: four small functions over fixed-width integers |
| P16 | a fourth independent algorithm for the one quantity where implementations have real room to disagree |
| P20 | `std::optional` turns finding three from a convention into a type |
| P04 | `std::variant` reaches the same guarantee the Rust gets from an enum, and charges more for it |
| P07 | an allocator, again: a parameter pair and a refusal disappear |
| P10 | **`std::variant`, again, and this time on a promise the C's own header makes in prose** |

**The promise.** [../c/phaseledger.h](../c/phaseledger.h) says of its return
value: "Returns 0, or one of the refusals above having written nothing." The C
keeps that promise by validating every argument before it writes to `*out`,
which is discipline rather than a guarantee. Nothing in
`int phase_ledger(..., phase_ledger_t *out)` prevents a later edit from filling
half the struct and then refusing, and a caller that ignored the return value
would read those half written fields. Here the answer is a
`std::variant<Ledger, Error>` returned by value, so **a refusal carries no
ledger to read**: the promise is the type.

**It is a smaller gain than P04's, and that is worth saying rather than
inflating.** P04 had three shapes of answer, nothing, one span or two, and a
real chance of reading a span that was never written. This has one shape and one
unwritten struct. What is the same in both is the mechanism: **the C's
out-parameter makes the invalid state representable and the sum type does not.**

**And no allocator appears, which is the other half of an honest answer.** P12
and P07 each bought sixty lines and a refusal with a `std::vector`, because
their answers are a variable number of values. This project's answer is eight
charges, one per marker code, however long the trace, so there is nothing for an
allocator to do and `std::array<double, kCodes>` is the whole of it. What that
array does buy is small and real: the size is part of the type, so the loop that
clears it and the loop that reads it cannot drift from the declaration the way a
`double[8]` and a separate `PHASE_CODES` can.

**`std::get_if` and not `std::get`**, because `build_host.py` compiles every C++
filter with `-fno-exceptions` and `std::get` on the wrong alternative throws
`std::bad_variant_access`, which under that flag reaches `std::terminate`.
`std::get_if` returns a pointer and cannot fail. P04 reached the same place with
`std::visit`; both are checked at compile time and neither can throw.

## Every comparison here is exact, which governs the file

The oracle's currents are sums of powers of two with dt at 2^-10 seconds, and
the parity test compares the total and all eight per-code charges with `==` and
no tolerance, so the order of operations is part of the specification. Each of
these is the C's, mirrored rather than rederived:

| | |
|---|---|
| the total | every sample accumulated and **then** multiplied by dt |
| a run's charge | that run's samples accumulated and **then** multiplied by dt |
| the residual | the magnitude of `total - split` over the magnitude of `total` |
| a trace that drew nothing | a residual of one, reported rather than divided |

The one figure compared with a tolerance, `abs=1e-15`, is the residual, because
it is a quotient rather than a sum of powers of two. This file is held to that
same bound and not a tighter one, so all four implementations agree on what is
being claimed.

One predicate is spelled three ways and is the same comparison. The C guards the
interval with `!(dt_s > 0.0)` and so does this file, written out as
`not_above_zero`; the Rust writes `x.is_nan() || x <= 0.0` because clippy
rejects that shape as `neg_cmp_op_on_partial_ord`. All three refuse zero, every
negative value and any value that is not a number, and all three accept an
infinite interval, which no oracle case reaches and which it would be a
divergence to refuse here alone.

## And it still needs a build flag the Rust does not

The build passes `-ffp-contract=off`, because gcc at `-O2` fuses a multiply
followed by an add, rounding once where two operations round twice, and **the
two routes to a total in this file are exactly that shape**.
`python/tests/test_float_builds_disable_contraction.py` requires the flag, and
since Wednesday 28 October 2026 it reads a header as well as its source, which
is what makes the flag a checked requirement here rather than an author's habit.
[../rust/README.md](../rust/README.md) is about the other side: Rust fuses only
where an author writes `mul_add`, so there is no flag to pass and none to forget.

## The filter protocol

One verb, because this project is one function, and the same verb
[../rust/src/main.rs](../rust/src/main.rs) speaks.

```text
L <dt_s> <min_samples> <n> <current code>*n
    -> total=.. split=.. residual=.. runs=.. rejected=.. rejected_samples=..
       per0=.. per1=.. .. per7=..
```

**All eight per-code charges are printed, zeros included.** Codes 5 and 6 are
unused in the chapter, and a trace that contains one is the finding the field
exists for, so printing only the non-zero ones would hide exactly that case.

Every floating point value crosses the protocol exactly: `strtod` in, which is
correctly rounded, and `%.17g` out, which is enough decimal digits that every
`double` reads back as the same bits. The Rust uses `str::parse` and `{:?}`, the
shortest form with the same guarantee, so the two filters print different text
for one value and **the parity test compares numbers rather than strings**. One
test function drives both filters, so there is one comparison loop rather than
two places for it to weaken.

## What is not here

The PPK2, the trace it records, the marker pins and every measured charge. Those
need the probe and the board. There is no measured number in this file.
