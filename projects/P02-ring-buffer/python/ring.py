#!/usr/bin/env python3
"""P02's ring in Python: the structure, and not the subject.

**What this is and is not.** P02's subject is the memory-ordering argument: four
build-time choices, and the cost of each as a measured number on the board. None
of that is expressible here. The interpreter offers no release store, no acquire
load and no data memory barrier, and the global interpreter lock makes the
question unanswerable rather than merely hard.

What *is* expressible is the structure, and it is worth having for one reason:
the invariant and the wrap are language-independent claims, and a fourth
independent implementation of them is a fourth chance to catch a defect in the
other three. So this file exists to join `python/tests/test_ring_parity.py`,
which compares the trace every implementation produces for one operation list:
which bytes are accepted, which are refused and counted, the order they come
back in, and the counters across their own wrap.

**This narrows an earlier claim rather than reversing it.** Until Saturday 3
October 2026 this directory said there was no Python implementation at all,
because the interpreter cannot control memory ordering. That is still true and is
still the reason Python is absent from the ordering comparison. It was too broad
as a reason to write nothing: the container is not the ordering.

So, precisely:

  joins        the trace comparison: FIFO order, capacity, drop counting, and
               the free-running counters across their wrap
  cannot join  the ordering comparison, because the four modes do not exist here
  cannot join  any cycle or byte cost, because those need the board

The free-running counters are the one part that needs care in Python, and for
the opposite reason to C. In C they are `uint32_t` and the wrap is defined
unsigned arithmetic, which is what makes `head - tail` correct across it. Python
integers do not wrap, so every update is masked to 32 bits explicitly. A version
that forgot the mask would agree with the C for the first four billion bytes and
then diverge, which is exactly the kind of defect a trace comparison over a
deliberately wrapped counter is for.
"""
from __future__ import annotations

# The capacity the C is built with. A power of two, so the subscript is a mask
# and no slot is sacrificed to tell full from empty.
RING_SIZE = 256
RING_MASK = RING_SIZE - 1

# 32 bits, because that is the width of head and tail in the C struct and the
# wrap is part of the behaviour being compared.
COUNTER_MASK = 0xFFFFFFFF

# The four ordering modes the C selects with -DRING_BARRIER. Named here so the
# parity protocol can carry a mode and this implementation can say that it
# behaves identically in all four, which is itself a claim worth checking: a
# single-threaded trace must not depend on the barriers.
BARRIER_NAMES = {
    0: "none, not shippable",
    1: "compiler only, signal fence",
    2: "compiler and processor, one DMB",
    3: "acquire load and release store on the indices",
}


class Ring:
    """Single producer, single consumer byte ring.

    The invariant is the C's invariant: the consumer may read a slot only after
    the producer has published a head that includes it, and the producer may
    write a slot only after the consumer has published a tail that releases it.
    Single-threaded here, so the invariant holds trivially and the interesting
    part is the arithmetic.
    """

    def __init__(self, mode: int = 2) -> None:
        if mode not in BARRIER_NAMES:
            raise ValueError("RING_BARRIER must be 0, 1, 2 or 3")
        self.mode = mode
        self.buf = bytearray(RING_SIZE)
        self.head = 0        # written by the producer only
        self.tail = 0        # written by the consumer only
        self.drops = 0       # producer only: bytes refused because it was full

    def barrier_name(self) -> str:
        return BARRIER_NAMES[self.mode]

    def put(self, byte: int) -> int:
        """1 accepted, 0 refused and counted.

        The mask on the comparison is the whole arithmetic argument. In C
        `(uint32_t)(h - t)` wraps by definition; here the subtraction is masked
        to the same 32 bits so a wrapped head still gives the right occupancy.
        """
        used = (self.head - self.tail) & COUNTER_MASK
        if used >= RING_SIZE:
            self.drops += 1          # policy, counted and not hidden
            return 0
        self.buf[self.head & RING_MASK] = byte & 0xFF
        self.head = (self.head + 1) & COUNTER_MASK
        return 1

    def get(self) -> int | None:
        """The byte, or None when empty."""
        if self.head == self.tail:
            return None
        byte = self.buf[self.tail & RING_MASK]
        self.tail = (self.tail + 1) & COUNTER_MASK
        return byte

    def used(self) -> int:
        return (self.head - self.tail) & COUNTER_MASK
