# The bit order

This document is normative. Where an implementation disagrees with it, the
implementation is wrong. Where `test/vectors.json` disagrees with it, one of the
two is wrong and the disagreement is resolved by hand on paper, never by running
code and adopting whatever it printed.

Written Thursday 1 October 2026, before any encoder or decoder in this
repository existed.

## The three sentences

1. The most significant bit of the first field is the most significant bit of
   byte zero, and it is the first bit on the wire.
2. Fields follow in the order given in `codec/fields.py`, with no padding
   between them and no alignment to any boundary.
3. A signed field is two's complement in its stated width.

Everything else in this repository is checked against those three sentences.

## What that gives for this layout

Forty bits, five bytes, no padding. Bit 39 is the first bit on the wire and
bit 0 is the last.

| Field | Width | Signed | Bit positions | Range |
|---|---|---|---|---|
| `version` | 3 | no | 39 to 37 | 0 to 7 |
| `flags` | 4 | no | 36 to 33 | 0 to 15 |
| `sequence` | 9 | no | 32 to 24 | 0 to 511 |
| `feature` | 18 | yes | 23 to 6 | -131072 to 131071 |
| `battery` | 6 | no | 5 to 0 | 0 to 63 |

Two fields straddle a byte boundary, which is the whole reason this document
exists. `sequence` occupies the last bit of byte 0 and all eight bits of
byte 1. `feature` occupies the low seven bits of byte 2, all of byte 3, and the
top two bits of byte 4.

## Worked by hand

These are the derivations behind `test/vectors.json`. They were done on paper
from the three sentences above, before `codec/payload.c` or `codec/payload.py`
was written, and they are reproduced here so that a reader can check the
arithmetic rather than trust it.

Writing the stream as forty bits in field order, then cutting it into bytes:

### All fields at their minimum, version 1

    version  001
    flags    0000
    sequence 000000000
    feature  000000000000000000
    battery  000000

    00100000 00000000 00000000 00000000 00000000
    0x20     0x00     0x00     0x00     0x00

### Every unsigned field at maximum, `feature` at its maximum positive value

The trap is here. `feature` is signed, so its largest positive value is
131071, which in eighteen bits is a zero followed by seventeen ones. It is not
eighteen ones. That zero is bit 17 of the stream, which is the top bit of
byte 2, so byte 2 is `0x7F` and not `0xFF`.

    version  111
    flags    1111
    sequence 111111111
    feature  011111111111111111      <- leading zero, value 131071
    battery  111111

    11111111 11111111 01111111 11111111 11111111
    0xFF     0xFF     0x7F     0xFF     0xFF

### Every bit set

Five bytes of `0xFF` is a real case, but it is not the case above. Decoding
forty set bits gives `feature = -1`, because eighteen set bits in two's
complement is -1. Both cases are in the vector file, because confusing them is
exactly the error this section documents.

    version 7  flags 15  sequence 511  feature -1  battery 63
    0xFF 0xFF 0xFF 0xFF 0xFF

### A negative `feature`, everything else ordinary

    version  001
    flags    0010
    sequence 000000101
    feature  111111111111111111      <- -1 in eighteen bits
    battery  101000

    00100100 00000101 11111111 11111111 11101000
    0x24     0x05     0xFF     0xFF     0xE8

### The most negative `feature`

    version  000
    flags    0000
    sequence 000000000
    feature  100000000000000000      <- -131072, only the sign bit set
    battery  000000

    00000000 00000000 10000000 00000000 00000000
    0x00     0x00     0x80     0x00     0x00

### `sequence` alone, to catch the byte straddle

Nothing but `sequence` set, to its maximum. Its nine bits are the last bit of
byte 0 and all of byte 1, so a reader that misplaced it by one bit cannot
produce this.

    version  000
    flags    0000
    sequence 111111111
    feature  000000000000000000
    battery  000000

    00000001 11111111 00000000 00000000 00000000
    0x01     0xFF     0x00     0x00     0x00

## A correction to the published chapter

Chapter 9 of the volume prints its second golden vector as

    {"fields": {"version": 7, "flags": 15, "sequence": 511,
                "feature": 131071, "battery": 63},
     "bytes": "FFFFFFFFFF"}

Those two halves contradict each other, and the bytes are the half that is
wrong for the stated fields. `FFFFFFFFFF` decodes to `feature = -1`. The fields
as written encode to `FFFF7FFFFF`. This repository carries both cases as
separate vectors rather than one wrong one, and the chapter's own rule applies
to the chapter as much as to a reader: the hand computation wins, and the
vector is not changed to match an implementation.

The irony is worth recording rather than smoothing over. The chapter says the
all-ones case is "the one that catches a width error", and in printing it the
chapter made a sign error instead.
