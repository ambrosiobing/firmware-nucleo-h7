# P08 in Python

**State: written Saturday 3 October 2026, and proven on the host the same day.**
Python needs no compiler, so this one went from written to proven in a single run
on win11 aquamarine: it agrees with the C over all twenty sequences and all 2674
events, including the one that drives the sequence number to its wrap at 512.

| File | What it is |
|---|---|
| `node_sm.py` | the eighteen rows, in the C's order, and a nine-line dispatcher |

The encoder is P09's: `firmkit.payload.encode`, the same module P09's own tests
drive and the file copied to the board under MicroPython. Calling it rather than
reimplementing it is what makes the shared acceptance criterion true by
construction.

## What Python costs here, and it is not what it costs elsewhere

For P02 the answer was that the language cannot express the measurement at all,
because it has no control over memory ordering. For P08 it can express
everything: the table is data, the guards are predicates, the actions change a
dataclass. Nothing is lost, and this is the shortest of the four
implementations.

What it cannot do is run on the board the way this volume needs. The MicroPython
route would carry the table and the dispatcher unchanged, and would not carry the
periodic wake the node depends on, which is P07's subject. So this file is a
deliverable for the host half and the reference the other three are read
against, not a candidate for the target.

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

- Not run under MicroPython on the board. The table and the dispatcher would
  carry unchanged; the periodic wake would not, and that is P07's.
- No timing claim of any kind. The dispatcher's cost in Python is not a figure
  this volume would quote.
