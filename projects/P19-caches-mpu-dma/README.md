# P19: Caches, the MPU, and why DMA reads stale bytes

Status: c=board cpp=none python=host rust=host

**Half done, and measured on the board Saturday 3 October 2026.** Taken out of
sequence because it blocked every cycle figure in the volume: a cycle count that
changes with where the code sits is a property of the build rather than of the
code, and until that was settled no other project could report one.

**What is done.** The instruction cache enable, which is architectural and read
from ARM's `cachel1_armv7.h` rather than from RM0455. It lives in the shared
`c/board/icache.c` because every project's figures depend on it, and it is
deliberately not called from `board_init`, so a project that wants it says so.

**What it measured, on P09.** The cache is worth 1.33 to 1.35 times. More
importantly it removes the placement dependence entirely: cold, the cost tracks
the offset within a 32 byte granule, 3220.99 cycles at offset 0 against 3163.98 at
offset 16; cached, it is 2378.99 at all four offsets. Pure placement is worth 57
cycles, 1.8 per cent, which is at most a quarter of the 7.8 per cent once
attributed to it.

**What it is, in full.** The corruption reproduced on purpose across a thousand
runs, then removed three ways, with the naive variant failing at a measured rate.

**And since Wednesday 28 October 2026 the alignment arithmetic is here, which
gives this project its first twin.** Criteria 4 and 5 are arithmetic: every
buffer aligned to 32 bytes and sized to a multiple of 32, enforced at build time
rather than by a comment, and a deliberately misaligned region table entry
stopping the build rather than the board. Neither needs a transfer engine or
RM0455.

| Piece | What proves it |
|---|---|
| `c/mpualign.c`, the maintenance range, criterion 4 and the region encoding | 6 ranges, 6 range refusals, 4 criterion 4 rows, 3 region encodings, 4 region refusals |
| `python/mpualign.py`, the twin, standard library only | the same cases, and the two readings the chapter's wording invites kept unused |
| `rust/src/lib.rs`, `no_std` and unsigned throughout, and `rust/src/main.rs`, the filter | the same cases through a three verb protocol, seven unit tests, and finding four refused by `checked_next_multiple_of` rather than by widening to 64 bits |
| `vectors.json`, the oracle | **compared exactly**, because all of it is unsigned integer arithmetic |

**This was the only project in the volume with one implementation**, and
therefore the only one with no equivalence gate at all. That comparison found a
real defect in P16 on Tuesday 27 October 2026 and another in P20 on Wednesday 28
October 2026; P19 had nothing it could disagree with until now.

**Four findings, and the first is why criterion 4 is an assertion rather than
advice.** A clean and an invalidate use the same outward rounding, and a line
holding the buffer's bytes and a neighbour's is harmless to clean and
destructive to invalidate, because that discards a write the processor has made
and not yet written back. The chapter states the rule and not the reason, so the
two shared end lines are reported separately here.

**Criterion 4's two halves fix different ends.** Aligning the start removes the
shared head line and sizing to a whole number of lines removes the shared tail;
a buffer of 40 bytes at a 32 byte boundary passes the alignment half and
discards the 24 bytes after it on an invalidate.

**The MPU aligns to the region's own size and not to the cache line**, so
criteria 4 and 5 are independent: a 4096 byte region at 0x24000100 satisfies
criterion 4 exactly and the hardware refuses it.

**And a range ending at the top of the address space has no representation.**
One past the last address needs 2 to the 32nd and a `uint32_t` has only zero to
offer, and zero makes a loop from base to end run no times, maintaining nothing
without a fault. It is refused rather than encoded.

**What is still missing.** The data cache half, which needs a transfer engine to
make a stale read possible, and that needs RM0455. The MPU region table applied
at startup and the buffer placement fixes wait on the same document; the
arithmetic that will validate that table is now here and tested.

**What it needs on the bench.** Nucleo plus an ADXL345 breakout on a breadboard.

Chapter 19 of [the book][book] is the written design. When work starts here it
follows that chapter's acceptance criteria, and anything the chapter leaves as
"not measured" is either measured or still says so.

[book]: ../../chapters/19-caches.md
