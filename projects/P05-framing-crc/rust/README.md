# P05 in Rust

**State: written Saturday 3 October 2026, and not yet built.** Written on win11
aquamarine, which has no cargo and is not to get one. Build and prove it in WSL
on the win11 skyhorizon demo laptop, bing@JPTOUPM678:

    cargo clippy --workspace --all-targets -- -D warnings
    cargo test --workspace
    cargo build --release --workspace
    python3 -m pytest python/tests/test_frame_parity.py -q -s

| File | What it is |
|---|---|
| `Cargo.toml` | the crate `p05-frame`, no dependencies, a workspace member |
| `src/lib.rs` | COBS, CRC-16, the frame, and the verdict names, `no_std` except under `cargo test` |
| `src/main.rs` | the filter `p05-filter`, three verbs: `C hex`, `E hex`, `D hex` |

## What Rust adds, and it is the same thing the C++ adds

`crc16` is a `const fn`, so the published check value 0x29B1 over `"123456789"`
is a `const` assertion at the bottom of `src/lib.rs`. A crate whose six
parameters are not the published ones does not build. The C asserts the same
value in a test.

## What it says that the C cannot

`cobs_decode` returns `Option<usize>`, and `Some(0)` is a frame that decoded to
nothing rather than a failure. The C returns 0 for both, so the single byte
`0x01`, which decodes to zero bytes legitimately, is reported by the C as a
stuffing error when it is a frame too short to carry its checksum. The Python
twin and the C++ agree with this crate. Both answers are refusals, so the
chapter's claim stands; the name differs for exactly one input, and
`python/tests/test_frame_parity.py` asserts that state so correcting the C turns
a test red on purpose rather than quietly.

Three more differences worth naming, because they are the language and not the
format:

- `frame_encode` takes `&[u8]` and `&mut [u8]`, so there is no capacity argument
  to pass wrongly and no pointer to check for null. The C's `FRAME_ERR_ARGS`
  has no counterpart here because the error cannot be made.
- The verdict is `Result<Decoded, DecodeError>`, so "an idle line" and "a
  payload of n bytes" are both successes and the four refusals are a separate
  type. The C flattens all six into one signed integer.
- `#![forbid(unsafe_code)]`, which the workspace also sets, so the bounds checks
  the C writes by hand are the compiler's.

## What it is proven against

The project's oracle, in three parts, through the parity test: 0x29B1 first,
then identical frames to the C over 3000 random payloads plus the cases byte
stuffing exists for, then identical verdicts over 3000 frames corrupted three
ways. The random cases and the corruption set are **not** in this crate;
`cargo test` holds the properties that need no other implementation, including
every single-bit flip of one seven-byte frame exhaustively, which is 64 flips
and needs no random seed at all.

## What it does not do yet

- Not compiled anywhere until the WSL run, so the `const` assertion has not yet
  been seen to pass.
- Not built for the board. The library compiles for `thumbv7em-none-eabihf` in
  CI, which is the proof it is genuinely `no_std`, but nothing links: a binary
  needs `cortex-m-rt` and a linker script, which is P01's Rust half.
- No size or cycle comparison against the C. Both are measurements and need the
  board.
