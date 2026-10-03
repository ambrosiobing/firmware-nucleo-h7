# P08 in C++

**State: host only, proven Saturday 3 October 2026.** Written on win11
aquamarine, which compiles nothing, then built and proven the same day: `g++` in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and then CI at commit 22ec8ad, where it is
also compiled with the full warning set and `-Werror`.

**The filter had to be rewritten before that passed, and the reason is worth
keeping.** It read with `fgets` into `char line[4096]`, and one parity sequence
drives 512 complete cycles in a single line to reach the sequence number's wrap,
about 21,500 characters. `fgets` returned that line in six pieces and the filter
answered each piece, so it produced 25 answers for 20 requests. The state machine
was never wrong. It surfaced only because `run_filter` asserts exactly one answer
per request, and it is now read whole: a larger buffer would have moved the
defect rather than removed it, since the protocol states no line limit.

| File | What it is |
|---|---|
| `node_sm.hpp` | the whole implementation, header only: scoped enums, the table, the dispatcher |
| `node_sm_filter.cpp` | the filter the parity test drives, built to `build-host/node_sm_filter` |

The encoder is P09's own C++ header, `../../P09-payload-codec/cpp/payload.hpp`,
included rather than copied.

## What C++ adds here, and it is less than in P05

In P05 the answer was clear: `constexpr` turns the published check value into a
`static_assert`, so a wrong parameter does not compile. P08 has no such external
value, so there is nothing of that kind to assert.

What the language does buy is narrower, and worth stating plainly rather than
dressing up. The enums are scoped, so `State::Tx` and `Event::TxOk` cannot be
mixed up the way two plain C enums can, and the dispatcher cannot be handed an
integer that merely happens to be in range. The table and the dispatcher are both
`constexpr`, so a compiler can see through a whole dispatch, though nothing here
depends on that. The cost is that the table is an aggregate of function pointers
rather than a C array of them, which reads very nearly the same.

That is the honest report: on a table-driven state machine with no memory hazard
and no undefined behaviour to avoid, C++ buys type safety at the edges and
changes nothing in the middle.

## What it is proven against

P08 has no external fact to appeal to, unlike P09's hand-computed vectors or
P05's published check value: the transition table **is** the specification, and
`../STATES.md`, written before any code, says what each state and event means
but cannot say what bytes come out.

So the comparison is made stricter instead. `python/tests/test_node_sm_parity.py`
drives every implementation over the same twenty event sequences, 2674 events,
and compares **the row index each one took for every event**, not merely the
state it ended in. That pins the four tables to one order, which matters because
two rows share a from-state and an event: the pair of `TX_FAIL` rows in `TX`,
told apart only by their guards. The retry row comes first in all four. Put the
out-of-retries row first and the retry row becomes unreachable, the node gives
up after one attempt instead of three, and a comparison of final states alone
could miss it.

Two things anchor it to something outside P08:

- **The frame.** The transmit stub must hand over exactly what P09's encoder
  produces, and each language calls its own P09 rather than a copy. A frame that
  matches across four languages is also four independent confirmations of P09's
  bit layout.
- **The retry limit.** Three attempts, not four. The C had this wrong once, by
  counting failures where the guard read attempts.

The parity test was proven able to fail three ways, one per claim: the two
`TX_FAIL` rows swapped in the Python table turned the row comparison red naming
both row lists; the retry limit changed to four turned two tests red; and an
undesigned exit added to `FAULT` turned the fault test red with "Python handled
4 events, so FAULT has an exit".

## What it does not do yet

- Not built for the board. `node_sm.hpp` includes nothing the target lacks, so it
  would compile there, and that stays a claim until `add_firmware()` has a C++
  target, which is P01's C++ half.
- No size or cycle comparison against the C. Both are measurements and need the
  board.
