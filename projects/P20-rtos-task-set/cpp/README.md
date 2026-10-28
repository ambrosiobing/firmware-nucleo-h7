# P20 in C++

**State: host, since Wednesday 28 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `latstat.hpp` | header only: the histogram, the percentile ranks, the three-way verdict, the stack arithmetic |
| `latstat_filter.cpp` | four verbs on stdin, so one parity test drives this beside the Rust |

The six findings this project exists for are written out in
[../c/latstat.h](../c/latstat.h) and are not repeated here. One of them is
expressed differently in this language, and that is what this page is about.

## What C++ adds here, which makes five answers on record

The answer varies by project and keeping it accurate is the point of asking:

| | What C++ adds |
|---|---|
| P18 | one function template where the C needs two forty line copies |
| P12 | the C's lack of an allocator costs it sixty lines of fixed buffers, bounds checks and an insertion sort |
| P17 | almost nothing: four small functions over fixed-width integers, which C expresses exactly as well |
| P16 | a fourth independent algorithm for the one quantity where implementations have real room to disagree |
| P20 | `std::optional` turns **finding three** from a convention into a type |

## Finding three, in the type system

Finding three is that a percentile whose rank falls inside the overflow count
has no bin, and that **reporting the top bin for it is the one wrong answer
nothing downstream can detect**.

The C carries that as two fields:

```c
uint32_t median_us;      /* the bin, meaningful only when resolved */
uint8_t median_resolved; /* zero when the rank fell inside over    */
```

Those can disagree, and nothing stops a caller reading `median_us` without
looking at the flag. The value sitting there is zero, **which reads as the
fastest run ever measured** rather than as an absent answer. That is the same
failure shape the C's own `LAT_ERR_EMPTY` refusal exists to prevent, reachable
here by a reader rather than by a caller.

Here `bin_at_rank` returns `std::optional<std::uint32_t>` and the report holds
optionals, so the value cannot be reached without the question being answered
first. The compiler enforces what the C leaves to a convention.

**What this is not.** It is not a better algorithm, it is not fewer lines, and
it is not a claim the C could not do it: a tagged struct or an accessor would
get there too. It is one specific thing: on the finding where a silent wrong
answer was the risk, the risk moves from the reader's discipline to the type.

**And it is given up at the boundary, deliberately.** The oracle compares seven
named numbers across four languages and an optional has no spelling in that
protocol, so the filter prints `median` and `median_resolved` as the C's struct
carries them. The guarantee holds where the arithmetic is and ends where the
wire format starts, which is the honest place to lose it rather than pretending
the whole pipeline has it.

## What is identical

The bin width of one microsecond and the 1024 bins of step 5; the truncating
division that turns cycles into microseconds; the pinned rank definition, the
sample of rank ceil(p times n) counting from one, computed in 64 bits because a
32-bit product wraps; the count checked against the bins plus the overflow
rather than trusted; the three-way verdict, where a pass needs the whole bin
below the budget and a failure needs the whole bin above it; **the strict
comparison of finding six**, because `bin + 1` wrapped for a maximum at the top
of a `uint32_t` and returned a pass for the worst run the instrument can
record; the eighty percent stack rule as a comparison rather than a division;
and the six refusals with their names.

## What else the language gives, which is the usual short list

`std::optional<Error>` makes a refusal a different type rather than a negative
return code, `std::array` fixes the 1024 bins without a macro, and
`std::accumulate` sums them in one line where the C writes a loop.

## The filter protocol

Four verbs, counts before data, samples run-length encoded because the chapter's
run is a million releases. The same four `../rust/src/main.rs` speaks.

```text
R <cycles_per_us> <npairs> <cycles repeat>*npairs    record a run, report, judge
B <over> <max_us> <count> <npairs> <bin count>*npairs  a histogram built directly
D <count_before> <cycles> <cycles_per_us>            one sample onto a preset counter
S <budget> <free>                                    one task's stack
```

Values are read as tokens and converted with `strtoul`, and a token with
anything left in it is rejected, which is P18's lesson of Monday 26 October 2026
kept even though this project has no floating point for the stream extractor to
mishandle. A request with anything left over is refused.

## One build flag that is absent, and why that is now checked

There is **no** `-ffp-contract=off` here, because every quantity in this project
is an integer. That absence used to be unverifiable: the guard in
`python/tests/test_float_builds_disable_contraction.py` read only the source
named in `build_host.py`, so a header-only library could have hidden a `double`
from it. Since Wednesday 28 October 2026 it reads the header too, which P16's
C++ forced, so the flag's absence here is a checked fact rather than an
assertion.

## What is not here

Not the board, and not FreeRTOS. There is no measured number in this project:
the budgets are quoted from table 20.4, which was written before any
measurement, and this is the arithmetic that will judge the figures when they
arrive.
