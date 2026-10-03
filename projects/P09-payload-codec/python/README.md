# P09 in Python

**State: host only.** `listen.py` is the edge-host decoder, which reads frames
from a serial link and decodes them with the same module the tests use.

The implementation itself is shared, in `python/firmkit/payload.py`, because it is
three things at once: the test oracle, this decoder, and the file that is copied
to the board unchanged under MicroPython. That third use is why
`python/tests/test_micropython_subset.py` exists and why
[../MICROPYTHON.md](../MICROPYTHON.md) carries no code.

`listen.py` needs `pyserial`, guards the import and refuses cleanly rather than
failing at the point of use.


## What it is proven against

Vectors.json, four hand-computed golden vectors, the fourth of them negative.
