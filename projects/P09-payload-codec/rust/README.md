# P09 in Rust

**Nothing here yet, and this is the first Rust crate the volume should carry.**

A `no_std` crate encoding and decoding the same 40-bit payload, driven against
the same `vectors.json`, with `panic = "abort"` and `opt-level = "s"`. The host
half is `cargo test` plus a small command that prints the hex for each vector,
which is how the parity test reaches it without a C ABI to maintain.

It is first because everything it needs already exists: four hand-computed
vectors, three implementations to disagree with, and a board image that prints
the same bytes.


## What it is proven against

`vectors.json`, four hand-computed golden vectors, the fourth of them negative.
