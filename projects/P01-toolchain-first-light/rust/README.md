# P01 in Rust

**The host half is here and the board half is not**, which the status line says
as `rust=host`.

`p01-clocktree` is this project's clock tree decode as a `no_std` crate with no
dependencies: masks, shifts, integer division and one rounding rule, so nothing
in it needs `core`'s floating point, let alone `libm`. The `p01-filter` binary
speaks the protocol in [`../PROTOCOL.md`](../PROTOCOL.md), which the C++ filter
also speaks, so one parity test drives both.

**This is the language that makes the fewest of these decisions for you**, which
is why it was worth writing third:

- integer overflow panics in debug and wraps in release, so the guard against the
  PLL product leaving 32 bits is a comparison and not a consequence of the type.
  Without it this crate would panic under `cargo test` and silently wrap under
  `--release`: two different wrong answers from one source file.
- `/` truncates toward zero, as C's does, so the frequencies need no special
  handling. Halves away from zero in the parts-per-million arithmetic is not
  free, and neither `i64::midpoint` nor a cast through `f64` would give that rule.
- `as i32` wraps and `i32::try_from` returns an error the protocol has no answer
  for, so the saturation is written out.

`cargo test` carries three cases of its own, so that running it alone says
something true: the two board rows, the rounding boundary above and below, and
the overflow refusal. The whole oracle belongs to the parity run.

Still to come: a `cortex-m-rt` blink and a UART banner on
`thumbv7em-none-eabihf`, with its own vector table and reset handler rather than
this repository's C startup, because the point is to compare two complete routes
to first light and not to borrow half of one.

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

The board itself is the second and is not reached yet: LD1 green on PB0
blinking, the banner on COM13 at 115200, and the clock the part reports about
itself.