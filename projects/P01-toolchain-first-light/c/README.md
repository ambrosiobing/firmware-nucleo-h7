# P01 in C

**State: runs on the board, since Friday 2 October 2026.**

`main.c` is first light: the three LEDs, the button, the banner, the clock
decoded from RCC, and the delay loop calibrating itself against `DWT_CYCCNT` on
every boot. Everything it leans on is in the shared `c/board/` and is shared
because every project links it.

Built on win11 skyhorizon with the toolchain bundled in CubeIDE 2.2.0, flashed by
copying the `.bin` to the probe's disk. Never compiled on win11 aquamarine.


## What it is proven against

The board itself: LD1 green on PB0 blinking, the banner on COM13 at 115200, and
the clock the part reports about itself.