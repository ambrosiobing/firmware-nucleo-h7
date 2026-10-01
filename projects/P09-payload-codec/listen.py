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

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "codec"))

import payload  # noqa: E402

try:
    import serial
except ImportError:
    serial = None


def decode_cobs(frame: bytes) -> bytes:
    """Chapter 5 owns this. Not reimplemented here from memory.

    When chapter 5's repository exists, this import replaces the stub:

        from cobs_c5 import decode_cobs

    Writing a byte-stuffing decoder a second time, in a second repository, from
    recollection of the first, is how two implementations of one format come to
    disagree. That is the failure mode this whole chapter is arranged against,
    so the stub refuses rather than approximates.
    """
    raise NotImplementedError(
        "chapter 5's COBS decoder is not available yet. See the docstring."
    )


def crc16(body: bytes) -> int:
    """Chapter 5 owns this too, and the polynomial must match the hardware unit.

    The CRC peripheral's reversal and initial-value settings are the documented
    trap here, and getting them wrong produces a checksum that is
    self-consistent on the host and disagrees with the board. So this is not
    guessed either.
    """
    raise NotImplementedError(
        "chapter 5's CRC-16 is not available yet, and its polynomial must match "
        "the peripheral's configuration. See the docstring."
    )


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
    while True:
        b = port.read(1)
        if not b:
            continue
        if b == b"\x00":                      # the frame delimiter
            try:
                frame = decode_cobs(bytes(buf))
            except NotImplementedError as exc:
                print(exc)
                return 1
            finally:
                buf.clear()
            body, got = frame[:-2], int.from_bytes(frame[-2:], "big")
            if crc16(body) != got:
                print("checksum mismatch, frame discarded")
                continue
            print(payload.decode(body))
        else:
            buf += b


if __name__ == "__main__":
    sys.exit(main(sys.argv))
