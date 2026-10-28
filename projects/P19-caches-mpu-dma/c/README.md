# P19 in C

**State: half done, and measured on the board Saturday 3 October 2026.**

The instruction cache enable is architectural, read from ARM's `cachel1_armv7.h`
rather than from RM0455, and it lives in the shared `c/board/icache.c` because
every project's cycle figures depend on it. It is deliberately not called from
`board_init`, so a project that wants it says so.

**And since Wednesday 28 October 2026 this directory holds `mpualign.c`**, which
is chapter 19's criteria 4 and 5: the maintenance range rounded outward to whole
lines with the two shared end lines reported separately, criterion 4's two halves
as one answer, and the ARMv7-M region encoding with the base checked against the
region's own size. All of it integer arithmetic, none of it needing a transfer
engine or RM0455, and **it is the first thing in this project with a twin**, so
P19 now has an equivalence gate where before it had none.

Four findings are written out in [mpualign.h](mpualign.h): a clean may
over-extend a range and an invalidate may not, criterion 4's two halves fix
different ends, the MPU aligns to the region's own size rather than to the cache
line so criteria 4 and 5 are independent, and a range ending at the top of the
address space has no representation for its end and is refused rather than
encoded as a zero that reads as "nothing to do".

Still missing: the data cache half, which needs a transfer engine to make a stale
read possible, and that needs RM0455. The instruction cache file this project
contributed is shared.


## What it is proven against

The instruction cache enable: P09's cycle count measured with it off and on, at
four placements, on the board on Saturday 3 October 2026.

`mpualign.c`: `../vectors.json` through `ctypes`, against the Python twin, in
`python/tests/test_mpualign_parity.py`. 6 maintenance ranges, 6 range refusals,
4 criterion 4 rows, 3 region encodings and 4 region refusals, compared exactly.