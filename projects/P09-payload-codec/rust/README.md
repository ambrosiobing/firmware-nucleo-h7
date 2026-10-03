# P09 in Rust

**State: host only, proven Saturday 3 October 2026** in WSL on the win11
skyhorizon demo laptop, bing@JPTOUPM678, with rustc 1.99.0. What that run
established, in the order it ran:

| Step | Result |
|---|---|
| `cargo fmt --all` | changed nothing |
| `cargo clippy --workspace --all-targets -- -D warnings` | clean |
| `cargo test --workspace` | 6 passed, 0 failed |
| `cargo build --release -p p09-payload --lib --target thumbv7em-none-eabihf` | compiles, which is the proof the codec is genuinely `no_std` |
| `python3 -m pytest python/tests/test_parity.py -q -s` | C, C++, Python and Rust all agree over the six vectors and 100000 recorded-seed cases, 4 passed |

For the hour between being written and being built it carried a fifth state,
**written**, added to this volume's vocabulary that day because the crate was
written on win11 aquamarine, which has no cargo and is not to get one. Calling it
host only then would have claimed a proof that had not happened; calling it
nothing would have denied a file that existed. The state is kept for the next
crate that needs it.

One thing the first build corrected. The release profile had been written into
this crate's `Cargo.toml`, where cargo ignores it in a workspace and says so, so
the filter was built with the defaults while this page claimed opt-level s. The
profile now lives at the repository root, where it applies.

## What is here

| File | What it is |
|---|---|
| `Cargo.toml` | the crate, no dependencies, which is a decision rather than an omission |
| `src/fields.rs` | the field table, **generated** from `../fields.py` by `python/tools/gen_codec.py`, the same way `c/payload/payload_fields.h` and `python/firmkit/payload_fields.py` are |
| `src/lib.rs` | the codec, `no_std` except under `cargo test` |
| `src/main.rs` | the filter, `p09-filter`, which speaks the protocol the C++ variant already speaks |

The crate is a member of the workspace at the repository root, so the filter is
built to `target/release/p09-filter` and there is one `Cargo.lock` for the whole
repository rather than one per crate.

## What it is proven against

`../vectors.json`, four of whose six vectors exist to catch a defect that
positive test values hide. The vectors are **not** copied into this crate and
`cargo test` does not read them. That is deliberate:

- The oracle belongs to the project, not to a language. A copy here would be a
  second thing to keep in step, and the whole reason the field table is generated
  is that a layout written out twice diverges silently.
- Parsing JSON here would mean a dependency, or thirty lines that test nothing,
  and it would limit the comparison to six cases.

So the division is: `cargo test` proves the properties that need no oracle, and
`python/tests/test_parity.py` drives this crate's filter, the C library, the C++
filter and the Python module against the one oracle, over the six vectors and
over the same hundred thousand recorded-seed cases. That test refuses a run in
which fewer than three of the four languages were actually compared, because a
parity suite that passes because nothing was built has proven nothing.

## The finding worth printing, which is not that Rust is safer

The C implementation is written around three hazards and says so in its
comments: shifting a negative `int32_t` right is implementation defined,
converting an out-of-range unsigned value to a signed type was implementation
defined before C23, and an unsigned subtraction that wraps hides the problem
rather than reporting it. Its `sign_extend` is written the long way round
specifically to avoid all three.

None of the three is expressible here. Rust defines `>>` on a signed integer as
an arithmetic shift, defines `as` between integer types as a wrapping two's
complement conversion, and makes an overflowing subtraction a panic in debug and
a defined wrap in release. So `sign_extend` here is two shifts, and the honest
report is that the hazard is **absent** rather than avoided.

That is the comparison chapter 9 can make, and it is the reason both versions are
written by hand rather than one generated from the other: the comparison only
means something if each is idiomatic for its own language.

## What this does not do yet

- **Not built for the board.** The library compiles for
  `thumbv7em-none-eabihf`, which is what proves it is genuinely `no_std`, but
  nothing links: a binary needs `cortex-m-rt` and a linker script, and that is
  P01's Rust half, which is not written. Until then no Rust figure in this volume
  came from the part.
- **No size or cycle comparison against the C.** Both would be measurements, and
  a measurement needs the board.
- **`SIGNED_FEATURE` in `src/fields.rs` is unused.** It is generated for all four
  languages from one specification, and the codec names the signed field
  directly. Removing it from the generator to tidy one language would make the
  four tables differ.
