# P02 in Rust

**State: host only, proven Saturday 3 October 2026** in WSL on the win11
skyhorizon demo laptop, bing@JPTOUPM678, with rustc 1.99.0, and then in CI at
commit 3b301e2. What that run established:

| Step | Result |
|---|---|
| `cargo fmt --all` | reformatted the filter's state function, committed as its own change |
| `cargo clippy --workspace --all-targets -- -D warnings` | clean on the first run |
| `cargo test --workspace` | 4 passed, including the four-mode trace comparison |
| `cargo build --release --workspace --lib --target thumbv7em-none-eabihf` | compiles, which is the proof the crate is genuinely `no_std` |
| `python3 -m pytest python/tests/test_ring_parity.py -q -s` | all four agree over 9620 operations in each of the four modes |

Clippy had nothing to say about the const generic compared with `==` in a
branch, which was the thing most likely to draw a lint. This and the C++ were
the first pair in the volume to compile without a correction.

| File | What it is |
|---|---|
| `Cargo.toml` | the crate `p02-ring`, no dependencies, a workspace member |
| `src/lib.rs` | the ring, `no_std` except under `cargo test`, the mode a const generic |
| `src/main.rs` | the filter `p02-filter`, the four verbs the C++ filter speaks |

`Ring<0>` through `Ring<3>` are four types the compiler monomorphises, each
emitting exactly the barriers it asks for. The C builds four objects and the C++
instantiates a template; all three arrive at the same place by different routes,
which is worth a paragraph in the chapter because it is one of the few places the
route differs enough to matter.

## What this crate honestly does not do, and it is the finding

The C's `ring_t` is one struct holding both indices, handed to a producer in
thread mode and a consumer in an interrupt handler. **That shape is not
expressible in safe Rust.** Two contexts holding `&mut` to one value is exactly
what the borrow checker exists to refuse, and it is right to refuse it, because
the C's version is sound only by an argument the compiler cannot see: a prose
claim about which context touches which field.

The honest Rust shape is a split, a `Producer` and a `Consumer` handle over one
buffer, which needs either `UnsafeCell` behind a safe interface, or a crate that
already did that work, or atomics covering the data as well as the indices. This
crate does none of those. Its methods take `&mut self`, so it is a correct
sequential ring, the host trace is a fair comparison, and the two-context split
is named as missing rather than faked.

That cuts against the easy conclusion. The C compiles a structure whose soundness
rests on a written argument; Rust will not compile that structure without
`unsafe`, so the choice is to encode the argument in a type or to opt out of the
checking. Neither is free, and which one this crate took matters more than the
fact that it contains no `unsafe`.

`cargo test` carries four properties that need no other implementation: the
capacity boundary with no slot sacrificed to tell full from empty, the counters
across their own wrap, that the trace is identical in all four modes, and that
each mode names itself.

## What it is proven against

P02's oracle is a trace, not a value: for one list of operations, which bytes
are accepted, which are refused and counted as drops, the order they come back
in, and the two free-running counters.
`python/tests/test_ring_parity.py` drives every implementation over three lists,
9620 operations, in each of the four ordering modes, and compares every answer
against the C.

The counters are in the comparison on purpose. `used` is derived from
`head - tail`, so an implementation that wrongly masked its counters to the
capacity would agree on `used` forever and disagree on `head` at once. A fourth
comparison therefore drives the counters past their own 32-bit wrap, which the
trace lists cannot reach in a reasonable number of operations.

**All four modes must give identical traces.** A barrier constrains visibility
between two contexts and says nothing about one context's own sequence, so a
single-threaded trace that changed with the mode would mean a mode was touching
the data path rather than the ordering. The C builds four objects, the C++
instantiates a template four times and the Rust monomorphises a const generic;
the test checks that the three routes arrive at the same place.

Proven able to fail three ways, one per claim: the capacity reduced by one,
which turned the fill list red at operation 256 naming both answers; the drop
increment removed, red at operation 258; and the 32-bit mask removed from the
Python counter, which the trace lists do **not** catch, because the first four
billion operations agree, and which the wrap comparison caught at byte 3 with
"the C head is 0 and the Python head is 4294967296".

## What no host can compare

The cost of each ordering mode, which is P02's actual subject. Those are board
figures, measured with the instruction cache on: the compiler fence costs 0.00
cycles and 0 bytes while still emitting different instructions, one DMB in put
and get costs 22.00 cycles and 16 bytes, and acquire plus release costs 29.00
and 24. Nothing in this directory can produce or check any of that.
