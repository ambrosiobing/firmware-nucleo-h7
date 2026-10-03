#!/usr/bin/env python3
"""The edge-host variant: frames in on the serial port, fields out.

    python host/listen.py COM7           # win11 aquamarine
    python host/listen.py /dev/ttyACM0   # a Linux host

The point of this file is that it is not a new implementation. It imports the
same codec/payload.py the property test uses as its oracle, and the only thing
it adds is a different source of bytes. An edge host that reimplemented the
decoder would be a third thing to keep honest.

Not yet run. pyserial is not installed on win11 aquamarine as of
Thursday 1 October 2026, and there is no board on the other end of the cable, so
this is written against chapter 5's frame format and left to be proven when both
exist. The two framing helpers it needs are chapter 5's and are stubbed here
with a note rather than guessed at, because a frame decoder written from memory
is exactly the kind of code that appears to work.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "firmkit"))

import payload  # noqa: E402

try:
    import serial
except ImportError:
    serial = None


# P05 owns the framing, and it is now written and tested, so this imports it
# rather than reimplementing it. Writing a byte-stuffing decoder a second time
# from recollection of the first is how two implementations of one format come to
# disagree, which is the failure P09's whole chapter is arranged against.
#
# Until Thursday 1 October 2026 the two functions below raised NotImplementedError
# with a note saying exactly that. P05's twin replaced them, and
# python/tests/test_frame.py holds it against the C the board links, over 3000
# random payloads, with every single-bit corruption rejected.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "P05"))

import twin  # noqa: E402


def decode_frame(stuffed: bytes):
    """What lay between two delimiters, to (verdict, payload).

    The verdict is P05's, and the distinct values matter: a stuffing error means
    the receiver lost its place, a checksum error means the line corrupted a byte,
    and counting them together would hide which.
    """
    return twin.decode(stuffed)


def main(argv: list[str]) -> int:
    if serial is None:
        print("pyserial is not installed. Install it with: pip install pyserial")
        return 1
    if len(argv) != 2:
        print(__doc__.splitlines()[0])
        print("usage: python host/listen.py <port>")
        return 2

    port = serial.Serial(argv[1], 115200, timeout=1)
    buf = bytearray()
    counts: dict[str, int] = {}
    while True:
        b = port.read(1)
        if not b:
            continue
        if b == b"\x00":                      # the frame delimiter
            verdict, body = decode_frame(bytes(buf))
            buf.clear()

            if verdict == "empty":
                continue                      # an idle line, not an error

            counts[verdict] = counts.get(verdict, 0) + 1
            if verdict != "ok":
                # Named rather than totalled. A stuffing error means the receiver
                # lost its place and a checksum error means the line corrupted a
                # byte, and one combined count would hide which.
                print("frame discarded: {}   totals {}".format(verdict, counts))
                continue

            print("{}   totals {}".format(payload.decode(body), counts))
        else:
            buf += b


if __name__ == "__main__":
    sys.exit(main(sys.argv))
