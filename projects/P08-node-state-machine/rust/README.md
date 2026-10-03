# P08 in Rust

**State: written Saturday 3 October 2026, and not yet built.** Written on win11
aquamarine, which has no cargo. Build and prove it in WSL on the win11 skyhorizon
demo laptop, bing@JPTOUPM678:

    cargo clippy --workspace --all-targets -- -D warnings
    cargo test --workspace
    cargo build --release --workspace
    python3 -m pytest python/tests/test_node_sm_parity.py -q -s

| File | What it is |
|---|---|
| `Cargo.toml` | the crate `p08-node-sm`, and the workspace's first inter-crate dependency |
| `src/lib.rs` | the eighteen rows, `no_std` except under `cargo test` |
| `src/main.rs` | the filter `p08-filter`, the protocol the C++ filter speaks |

## The first inter-crate dependency in this workspace, and why

`p09-payload` is a path dependency rather than a second copy of the codec. P08's
acceptance criterion, shared with P09, is that the transmit stub's bytes equal
what P09's encoder produces for the same input. Depending on the crate makes that
true by construction; a copy here would make it true by agreement, which is the
thing the whole four-language comparison exists to avoid. The C does the same
with `payload.h`, the C++ with `payload.hpp`, the Python with `firmkit.payload`.

It also means `Cargo.lock` now records an edge rather than two unrelated
packages, and the `cargo metadata --locked` gate in CI checks that edge along
with the rest.

## What Rust adds here, and it is less than in P05

`State` and `Event` cannot be confused with each other or with an integer, and a
row action cannot return a value the dispatcher silently ignores. That is the
whole list. There is no memory hazard to remove and no undefined behaviour to
avoid, because the C's table is already data.

One line is written differently and the reason is in the code: `act_encode`
matches on the encoder's `Option` rather than unwrapping it, even though the
buffer is exactly `PAYLOAD_BYTES` and the `None` arm is unreachable today. An
`unwrap` would be a panic in a `no_std` image, and a later change to the buffer
size should leave a `frame_len` of zero rather than a panicking handler on a board
with no console.

`cargo test` carries five properties that need no other implementation: every row
reachable, the retry limit allowing exactly three attempts, nothing leaving
`Fault`, the stub's bytes equalling what the `p09-payload` crate encodes, and the
sequence number wrapping where P09's nine-bit field does.

The first of those found a defect in itself on its first run, which is the best
argument for writing it the way it is written. Every sequence it drove started in
`Init`, where `Tick` takes the first row, so the `Idle` periodic wake row was
never reached and the test failed with `rows never taken: ["idle: periodic
wake"]`. The table was correct and the test's own list was short by one
sequence. A coverage check that reported a count rather than the row name would
have said `17 of 18` and left the reader to work out which.

## What it is proven against

P08 has no external fact to appeal to, unlike P09's hand-computed vectors or
P05's published check value: the transition table **is** the specification, and
`../STATES.md`, written before any code, says what each state and event means
but cannot say what bytes come out.

So the comparison is made stricter instead. `python/tests/test_node_sm_parity.py`
drives every implementation over the same twenty event sequences, 2674 events,
and compares **the row index each one took for every event**, not merely the
state it ended in. That pins the four tables to one order, which matters because
two rows share a from-state and an event: the pair of `TX_FAIL` rows in `TX`,
told apart only by their guards. The retry row comes first in all four. Put the
out-of-retries row first and the retry row becomes unreachable, the node gives
up after one attempt instead of three, and a comparison of final states alone
could miss it.

Two things anchor it to something outside P08:

- **The frame.** The transmit stub must hand over exactly what P09's encoder
  produces, and each language calls its own P09 rather than a copy. A frame that
  matches across four languages is also four independent confirmations of P09's
  bit layout.
- **The retry limit.** Three attempts, not four. The C had this wrong once, by
  counting failures where the guard read attempts.

The parity test was proven able to fail three ways, one per claim: the two
`TX_FAIL` rows swapped in the Python table turned the row comparison red naming
both row lists; the retry limit changed to four turned two tests red; and an
undesigned exit added to `FAULT` turned the fault test red with "Python handled
4 events, so FAULT has an exit".

## What it does not do yet

- Not compiled anywhere until the WSL run.
- Not built for the board. The library will compile for `thumbv7em-none-eabihf`
  in CI, which is the proof it is genuinely `no_std`, but nothing links: that
  needs `cortex-m-rt` and a linker script, which is P01's Rust half.
- No size or cycle comparison against the C.
