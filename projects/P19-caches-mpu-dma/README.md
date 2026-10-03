# P19: Caches, the MPU, and why DMA reads stale bytes

Status: c=board cpp=none python=none rust=none

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

**What is still missing.** The data cache half, which needs a transfer engine to
make a stale read possible, and that needs RM0455. The MPU region and the buffer
placement fixes wait on the same document.

**What it needs on the bench.** Nucleo plus an ADXL345 breakout on a breadboard.

Chapter 19 of [the book][book] is the written design. When work starts here it
follows that chapter's acceptance criteria, and anything the chapter leaves as
"not measured" is either measured or still says so.

[book]: ../../chapters/19-caches.md
