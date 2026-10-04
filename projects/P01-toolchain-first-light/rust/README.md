# P01 in Rust

**Nothing here yet.** The crate that goes here is a `cortex-m-rt` blink and a
UART banner on `thumbv7em-none-eabihf`, with its own vector table and reset
handler rather than this repository's C startup, because the point is to compare
two complete routes to first light and not to borrow half of one.

What was verified for this exact part, as against the family name, is in
[../../../rust/README.md](../../../rust/README.md).


## What it is proven against

**Two things, since Sunday 4 October 2026, and the host one can be done first.**

The host oracle is [`../clock_vectors.json`](../clock_vectors.json): 15 register
configurations and 8 bias rows, answered by a `no_std` crate with no dependency
on anything board shaped, and compared against the C through a parity test that
does not exist yet, in the shape `python/tests/test_gates_parity.py` shows. Rust
is the interesting language for
this particular file, because the rounding rule the bias needs, halves away from
zero on integers, is not what either language gives for free, and the saturation
at the ends of a signed 32 bit range is spelled differently in each.

The board itself is the second: LD1 green on PB0 blinking, the banner on COM13 at
115200, and the clock the part reports about itself.