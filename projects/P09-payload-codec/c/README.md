# P09 in C

**State: runs on the board, Saturday 3 October 2026.**

`main.c` encodes the negative golden vector on the Cortex-M7, prints it, decodes
it back and checks the round trip, so sign extension, bit order and field packing
are proven to agree between the two compilers. `pad.c` contributes a measured run
of zero bytes to the code section, which is how the placement experiment displaces
four otherwise identical images.

The codec itself is shared, in `c/payload/`, because P08 and P12 use it, and
`payload_fields.h` is generated from `fields.py` in this project directory.


## What it is proven against

Vectors.json, four hand-computed golden vectors, the fourth of them negative.
