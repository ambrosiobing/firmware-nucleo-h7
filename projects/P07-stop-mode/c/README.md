# P07 in C

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI.

| File | What it is |
|---|---|
| `cyclecharge.h` | the contract, the three findings at length, the seven refusals |
| `cyclecharge.c` | the segmentation on rising edges, the median and spread, the battery sum |

**Why the window starts where it does.** A cycle runs from the first sample at
which the marker is high having been low, not from the last sample at which it
was low. One sample either way is invisible on a flat trace and is a whole
cycle's worth of charge when a peak lands on the boundary, which is the second
finding in the header.

**The median needs a sort, and the sort is an insertion sort over at most 64
doubles.** No allocation, which is the same choice P12's gate made for the same
reason: this file could compile for the target even though it has no reason to
run there, and that property is what forced its shape.

**The floor is enforced rather than documented.** Step 8 says to report over at
least ten cycles and never a single average current. `cyc_report` refuses fewer,
because a median and a spread over three cycles is the shape of the number the
chapter asks for without being it.

## What it is proven against

5 traces, 5 reports, 4 lifetimes and 14 refusals, driven through this
implementation and its Python twin by
`python/tests/test_cyclecharge_parity.py`. Every charge is exact in binary: the
currents are powers of two and the sample rate is 1024 Hz, so nothing passes on
a tolerance. The C half of that test skips on a laptop with no compiler, naming
the WSL command, and runs in full in CI.

## What is not here

The instrument, the resistor check, the run-state inventory, the stop-mode entry
sequence, the clock restore and what it costs, the low-power timer variant, the
tickless variant and the marker pins. All of them need the board and the nRF
PPK2.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
