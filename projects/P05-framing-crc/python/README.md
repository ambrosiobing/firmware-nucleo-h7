# P05 in Python

**State: host only.** `twin.py` is the twin that corrupts frames on purpose, so
the rejection path is exercised by something other than the code that wrote the
frame.

It reaches the same 0x29B1 the C side reaches, from an independent implementation,
which is why the check value is evidence rather than a constant copied between two
files.


## What it is proven against

The published check value 0x29B1 and the rejection of every single-bit corruption.
