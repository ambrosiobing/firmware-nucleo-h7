# P06 in C

**State: links, all three back ends, since Friday 2 October 2026, and none has
been flashed.**

`acq_systick.c`, `acq_timer.c` and `acq_dma_double.c` are the three back ends,
selected at build time so the comparison is of mechanisms rather than of three
programs. `main.c` is the one application above them and `marker.c` drives the
marker pin the instruments watch.

Each back end refuses at run time rather than guessing a converter, timer or
transfer engine setting that RM0455 governs. A target binary that links is
evidence about the build and about nothing else.


## What it is proven against

The external witness: an edge analysis and a rate analysis that must agree
before any rate is claimed.