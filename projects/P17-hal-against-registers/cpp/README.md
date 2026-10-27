# P17 in C++

**State: host, since Tuesday 27 October 2026.** Written on win11 aquamarine,
which compiles nothing; the first compiler to see it is g++ in WSL on the win11
skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI.

| File | What it is |
|---|---|
| `tbmath.hpp` | header only, C++17: the four functions and the seven refusals |
| `tbmath_filter.cpp` | the same four verbs the Rust filter speaks, so one test drives both |

## What C++ adds here, and this time it is almost nothing

That answer varies by project in this volume and is worth keeping accurate rather
than consistent. P18's cascade is **one function template** where the C needs two
forty-line copies, which is a real gain and the first in the volume. P12 records
the opposite: the C's lack of an allocator costs it sixty lines the other three do
not have. Here the arithmetic is four small functions over fixed-width integers,
which C expresses exactly as well.

`std::optional<Error>` makes a refusal a different type rather than a negative
return code, and `std::array` fixes the sample buffer without a macro. That is
the whole of it, and saying so is the point: a language claim is worth something
only in a volume that also records where the language adds nothing.

**One thing is better and it is not the language's doing.** The subtraction
compares before subtracting, as the C does. The Rust twin's README records what
the chapter's printed expression does in four different ways depending on the
compiler and the profile, and the useful response to that is to not write the
expression, in any language.

## The two parsing lessons this filter inherited

**Values are read as tokens and converted with `strtod` and `strtoul`**, not with
the stream extractor. `in >> v` on a double cannot parse `inf` or `nan` in
libstdc++, and this project's oracle carries both as refusal cases for the wrap
period, so the protocol has to carry a value the implementation will reject. P18's
filter was corrected for exactly this on Monday 26 October 2026.

**A request with anything left over is refused**, which caught a real defect in
P18's test on the same day: ignoring a remainder lets a caller send one thing and
be answered about another.

## What it is proven against

The same oracle as the other three, driven through the filter: 15 cases and 15
refusals, compared exactly. The test runs over this filter and the Rust one with
the language in the test id, so a disagreement names which.

## What is not here

The bus read, which is one of chapter 17's four implementations and needs the
peripheral and the shield.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
