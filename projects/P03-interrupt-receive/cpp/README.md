# P03 in C++

**State: host only, proven Saturday 3 October 2026.** Written on win11
aquamarine, which compiles nothing, then built and proven the same day: `g++` in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and then CI at commit 92b0bf7, where it is
also compiled with the full warning set and `-Werror`.

The two `static_assert`s have been seen to pass, which is what the state above
rests on: a translation unit including this header would not have compiled if
either the clean step or the bridge case were wrong.

| File | What it is |
|---|---|
| `attribute.hpp` | the whole attribution, header only and `constexpr` |
| `attribute_filter.cpp` | the filter the parity test drives, three verbs: `A`, `F`, `R` |

## What C++ adds here

Two things. The attribution is `constexpr`, so two cases are `static_assert`ed at
the bottom of the header: the clean step and the bridge case. A translation unit
that includes the header does not compile if either is wrong, which is a stronger
statement than a test that must be run, and the bridge case is the one the whole
file exists for.

The refusal is a `std::optional<Row>` rather than a negative return code, which
makes the two outcomes different types instead of different ranges of one
integer. That matters more than it looks: the C returns `-1` and fills nothing,
so a caller that forgot to check reads a row of uninitialised fields.

## What it is proven against

Synthetic steps whose answers are known by construction, nineteen of them, driven
through every implementation by `python/tests/test_attribute_parity.py` and
compared against the C. The clean cases are not the interesting ones:

| Step | Why it is in the list |
|---|---|
| the bridge case | a tenth of the traffic never reached the peripheral and the target lost nothing. Reporting it as target loss would be the wrong conclusion, and this is why the file exists |
| the bridge and the target both lost bytes | the case the verdict **order** protects. BRIDGE is decided first, so this is BRIDGE; an implementation that decided the target first would answer LATENCY and publish a target limit from a run that never measured the target |
| overrun alone, drops alone, both | three different findings with three different fixes, and the same total loss in two of them |
| exactly at the tolerance, one byte past it | the boundary a careless edit to the tolerance would move silently |
| three counters that exceed a 32-bit sum | refused, rather than wrapped into a plausible bridge loss |
| over-accounted | the target claims more than the host sent, which is a measurement defect and is refused rather than clamped |

`first_loss` is compared too, over four ramps, because that is where the
confusion would actually be published: a ramp whose bridge gives up at a high
rate must not read as the target failing there, and all four skip bridge rows and
take the lowest rate rather than the first row.

**The list was found to have a hole, by mutation, and the hole is worth
recording.** Reordering the Python's verdicts so the target was decided before
the bridge changed no answer in the list, because no step lost bytes in both
places at once. Three files claimed that order was load bearing and nothing
checked it. Two steps were added and the order is now asserted by name, and the
same mutation turns the suite red.
