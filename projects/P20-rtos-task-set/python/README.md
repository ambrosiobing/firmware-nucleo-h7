# P20 in Python

**State: host, since Tuesday 27 October 2026.** The twin of
[../c/latstat.c](../c/latstat.c), standard library only, and it runs on every
laptop here including the one that compiles nothing.

| File | What it is |
|---|---|
| `latstat.py` | the same four functions, plus the two readings the chapter's wording invites, kept unused |

**Why a twin rather than a second opinion.** All of chapter 20's deciding
arithmetic is integer, and the C's answers depend on widths the C chose: a
64-bit rank product, a 64-bit bin walk, a comparison instead of a division for
the eighty percent rule. Python has none of those limits, so where the two agree
the width was right, and where they disagree the width is the reason. The oracle
carries a histogram with a count of four thousand million for exactly that
comparison: 999 times it overflows a 32-bit product, and this file would never
notice.

**It carries the two readings this project refuses, and nothing calls them.**

| | What it does |
|---|---|
| `chapter_two_way_verdict` | compares a percentile with its budget as though the percentile were a value rather than a bin one microsecond wide |
| `chapter_used_from_kernel` | reads `uxTaskGetStackHighWaterMark`, which is the smallest FREE space, as though it were the used figure |

Both follow criterion 4 and criterion 6 as written. A test runs each one beside
the pinned answer and prints what it would have reported: a pass on a run whose
every sample exceeds the budget, and a task at twenty percent that has actually
used eighty. **Carrying them unused is how this project records that the
chapter's wording permits them, rather than describing the problem in prose and
leaving it unrun.**

The pinned rank definition, the three verdicts and the five findings are in
[../README.md](../README.md), and what is not here is in the same place.
