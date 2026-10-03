# P05 in Rust

**Nothing here yet.** Framing and the hardware CRC unit has not started, and
this directory exists so the four language routes are visible from the beginning
rather than arriving one at a time.

What it will carry. The Rust implementation of framing and the hardware CRC
unit, proven against the same oracle as the other three languages, which this
project has yet to define.

What has to come first is in [../README.md](../README.md). Nothing here is written
until it can be proven, and an empty source file would be a claim rather than a
placeholder, which is why there is none.


## What it is proven against

The published check value 0x29B1 and the rejection of every single-bit corruption.
