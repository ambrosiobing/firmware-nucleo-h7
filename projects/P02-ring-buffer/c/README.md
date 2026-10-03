# P02 in C

**State: runs on the board.** Four images, one per ordering mode, producer in
thread mode and consumer in SysTick.

`main.c` is the target application. `property_test.c` and `soak_threads.c` are the
host proofs: the property test over the whole state space and the two-thread soak
at 21.6 million bytes per run. The structure itself is shared, in `c/ring/`,
because P03, P04, P05 and P11 all use it.

Re-measured Saturday 3 October 2026 with the instruction cache on, which
overturned the first table: see the project README for the figures that stand.


## What it is proven against

The four-mode soak: 21.4 million bytes through the ring with zero mismatches,
and a cycle and byte cost per ordering mode.