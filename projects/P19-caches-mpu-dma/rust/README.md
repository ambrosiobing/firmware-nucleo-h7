# P19 in Rust

**Nothing here yet.** Caches, the MPU, and stale DMA reads has not started, and
this directory exists so the four language routes are visible from the beginning
rather than arriving one at a time.

What it will carry. The Rust implementation of caches, the MPU, and stale DMA
reads, proven against the same oracle as the other three languages, which this
project has yet to define.

What has to come first is in [../README.md](../README.md). Nothing here is written
until it can be proven, and an empty source file would be a claim rather than a
placeholder, which is why there is none.


## What it is proven against

P09's cycle count measured with the instruction cache off and on, at four
placements.