# P18 in C++

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine, which
compiles nothing and never will; the first compiler to see it is g++ in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI, which compiles it under
the whole warning set.

| File | What it is |
|---|---|
| `dspaccept.hpp` | header only, C++17: one cascade template, the Q15 conversion, both metrics |
| `dspaccept_filter.cpp` | six verbs on stdin, one answer per line on stdout |

## What C++ adds here, and for once the answer is something

**The C carries two cascade functions, `biquad_df2t_f64` and
`biquad_df2t_f32`, forty lines each and identical but for the type of five
locals.** That duplication is not stylistic. It is two places a change has to be
made and one place it can be forgotten, inside the file whose whole subject is
whether two numeric formats agree. Here it is one function template instantiated
at `double` and at `float`, so the two paths cannot drift apart.

That is worth saying plainly because the honest answer in most of this volume has
been the opposite. P06's C++ header says C++ adds "very little, and that is the
honest answer"; P12's says the C's lack of an allocator costs it sixty lines the
other three do not have. Here the language expresses something about the problem
rather than about itself, and it is the only project so far where that is true.

`std::optional<Error>` makes a refusal a different type rather than a negative
return code, and the error names are the C's six in the same order, so a code
means the same thing in both.

## The protocol, and why the counts come first

Six verbs, each taking its counts before its data so the parser never guesses
where a list ends: `D` and `S` for the double and single cascades, `P` for the
smallest post shift, `Q` for the Q15 conversion, `R` for the Q15 run, and `A` for
both metrics. Doubles are printed with `%.17g`, which round-trips a double
exactly, so the parity test compares bit for bit on every case the oracle marks
exact. A single-precision result is promoted before printing, which is exact:
every float has an exact double.

**It reads whole lines rather than into a fixed buffer**, and that is a lesson
rather than a preference. P08's filter used `char[4096]`, met a request of twenty
one thousand characters on Saturday 3 October 2026, and answered twenty requests
with twenty five answers. The design-case request here carries 1024 samples on
one line, about twenty five thousand characters, so this project exercises that
lesson rather than inheriting it.

## One refusal this protocol cannot express

`accept_metrics` in the C takes **both** lengths, because comparing two blocks of
different lengths against the shorter of them returns a plausible number rather
than an error. That signature was changed on Monday 26 October 2026 after an
oracle case demanded a refusal the earlier one could not express. This filter's
`A` verb carries one count for both blocks, so the two vectors are the same
length by construction and the mismatch cannot arrive. The C++ function still
refuses it, for a caller that constructs two vectors itself; the filter simply
has no way to ask. The test says so where that case is handled rather than
quietly skipping it.

## What it is proven against

The same oracle as the C and the Python: 7 cascade cases driven four ways each,
the post shift for every one, 5 metric cases, 8 refusals, and the 1024 sample
design vector compared against both other implementations. Driven by
`python/tests/test_dspaccept_parity.py`, which skips on a laptop with no compiler
and names the build command.

## What is not here

What is not in the C either: the real transform and its packed output, the
complex magnitude, the cycle counter and the eight-case placement grid. Those
need the part.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
