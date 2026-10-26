# P07 in Python

**State: host, since Monday 26 October 2026.** The twin of
[`../c/cyclecharge.c`](../c/cyclecharge.c), and chapter 7's step 9 asked for a
host test of the segmentation in continuous integration with no instrument
attached. Standard library only.

| File | What it is |
|---|---|
| `cyclecharge.py` | the twin, and both of chapter 7's printed functions carried unused |

**This file carries step 8's segmentation and step 10's lifetime exactly as
printed**, rewritten without NumPy so they can be run rather than described.
`chapter_per_cycle_charge` is where both of the first two findings live: its
window starts at the rise index rather than one past it, and a trace beginning
with the marker high has one fewer complete cycle than it has pulses, which is
what step 9's own assertion gets wrong. `chapter_lifetime_days` is the battery
sum that leaves the uncovered part of the day free. Nothing above them calls
either, and the tests run both.

**The trace builder lives in the test, not here.** The oracle stores a recipe
and the test turns it into a trace in ten lines, so both implementations receive
the same arrays and the oracle stays small enough for a reader to check.

## What it is proven against

5 traces, 5 reports, 4 lifetimes and 14 refusals, driven through this
implementation and the C by `python/tests/test_cyclecharge_parity.py`. This half
runs everywhere, including on a laptop with no compiler, which is why the
findings are demonstrated through it.

## What is not here

Everything that needs the board or the instrument, and every measured number:
every trace this has seen was synthetic, which is what step 9 asks of it.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
