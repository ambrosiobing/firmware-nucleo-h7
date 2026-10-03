# P01 in Rust

**Nothing here yet.** The crate that goes here is a `cortex-m-rt` blink and a
UART banner on `thumbv7em-none-eabihf`, with its own vector table and reset
handler rather than this repository's C startup, because the point is to compare
two complete routes to first light and not to borrow half of one.

What was verified for this exact part, as against the family name, is in
[../../../rust/README.md](../../../rust/README.md).


## What it is proven against

The board itself: LD1 green on PB0 blinking, the banner on COM13 at 115200, and
the clock the part reports about itself.