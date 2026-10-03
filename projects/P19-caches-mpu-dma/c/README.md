# P19 in C

**State: half done, and measured on the board Saturday 3 October 2026.**

The instruction cache enable is architectural, read from ARM's `cachel1_armv7.h`
rather than from RM0455, and it lives in the shared `c/board/icache.c` because
every project's cycle figures depend on it. It is deliberately not called from
`board_init`, so a project that wants it says so.

Still missing: the data cache half, which needs a transfer engine to make a stale
read possible, and that needs RM0455. Nothing in this directory yet; the file this
project contributed is shared.


## What it is proven against

P09's cycle count measured with the instruction cache off and on, at four
placements.