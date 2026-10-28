# P19 in Python

**State: host, since Wednesday 28 October 2026.** The twin of
[../c/mpualign.c](../c/mpualign.c), standard library only, and **the first thing
in this project that anything can disagree with**.

Until today P19 held one implementation in one language, the instruction cache
enable in the shared `c/board/icache.c`, so it had no equivalence gate at all.
Every other project in the volume carries at least two implementations, and that
comparison found a real defect in P16 on Tuesday 27 October 2026 and another in
P20 on Wednesday 28 October 2026.

| File | What it is |
|---|---|
| `mpualign.py` | the maintenance range, criterion 4 and the region encoding, plus the two readings the chapter invites, kept unused |

## What it covers, which is criteria 4 and 5

Chapter 19's fourth criterion is that every buffer is aligned to 32 bytes and
sized to a multiple of 32, enforced by a build-time assertion rather than by a
comment. Its fifth is that a deliberately misaligned region table entry stops
the build rather than the board. **Both are arithmetic.** The stale read needs a
transfer engine and RM0455; deciding which buffers can produce one does not.

## Why this twin is worth comparing, and it is the widths

**The C computes on 32-bit addresses and this has no width at all.** That makes
one case a real comparison rather than something only the C can see: a buffer
ending exactly at the top of the address space needs an end value of 2 to the
32nd, and `uint32_t` has only zero to offer. Zero makes a loop from base to end
run no times, maintaining nothing without a fault or a diagnostic, which is the
chapter's own failure arriving by arithmetic. Both refuse it, and **this one
refuses it by the stated rule rather than by running out of bits**.

The same asymmetry runs the other way for the region size. The C needs no upper
bound, because every power of two a `uint32_t` holds is at or below 2 to the
31st, so such a guard could never fire there. This language has no width, so the
test is written here and refuses a size the C could not have been handed. That
keeps the two agreeing on every case the oracle can state rather than only on
the ones the C can reach.

## The two readings the chapter invites, carried unused

`chapter_align_only` reads criterion 4 as alignment alone, which is half of what
it says: it removes the shared head line and leaves the shared tail, and
invalidating that tail discards whatever follows the buffer.

`chapter_line_aligned_region` reads criterion 5 as alignment to the cache line,
which passes a 4096 byte region at 0x24000100 that the hardware refuses.

Nothing above calls either. Two tests in
`python/tests/test_mpualign_parity.py` run them beside the pinned answers and
print the disagreement, which is this volume's habit: a trap recorded is a trap
a test can fail on, where a trap described is prose.

## What is not here

The transfer engine, the stale read reproduced across a thousand runs, the three
fixes and their failure rates, the two cycle costs per transfer, the reachability
table against RM0455, and the fourth failure with its register evidence. Those
need the board, an ADXL345 on a breadboard and RM0455.

## What it is proven against

`../vectors.json`, which is this project's own oracle: 6 maintenance ranges, 6
range refusals, 4 criterion 4 rows, 3 region encodings and 4 region refusals,
compared exactly against the C through `ctypes`.
