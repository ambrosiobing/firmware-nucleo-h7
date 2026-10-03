#!/usr/bin/env python3
"""The Python twin of P05's framing, and the bit flipper that attacks it.

Two implementations of one format, in two languages, is normally a defect waiting
to happen. The same discipline as P09 turns it into the strongest test available:
if the C and the Python agree over thousands of random payloads, and both agree
with a check value neither of them computed, the format is real.

The check value is the part that makes this more than self-consistency. The
published checksum of the nine bytes "123456789" under these parameters is 0x29B1,
and every catalogue of these polynomials lists it. Both implementations are held
to that before anything else is believed.

THE BIT FLIPPER IS THE POINT OF THE TWIN. An encoder and a decoder that agree
prove the happy path. What a frame format is actually for is noticing when the
line has corrupted something, so this file corrupts frames on purpose and counts
what gets through. A format whose checksum has never been shown to reject anything
is a format with a checksum-shaped decoration on it.

Runs with no hardware. python/tests/test_frame.py drives it.
"""
from __future__ import annotations

import random

CRC16_POLY = 0x1021
CRC16_INIT = 0xFFFF
CRC16_CHECK = 0x29B1          # over b"123456789"
DELIMITER = 0x00
CRC_BYTES = 2


def crc16(data: bytes) -> int:
    """Polynomial 0x1021, initial 0xFFFF, no reflection, no final exclusive-or.

    Written to mirror the C reference line for line rather than to be clever, so
    that a disagreement points at one of them rather than at the translation.
    """
    crc = CRC16_INIT
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ CRC16_POLY) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(src: bytes) -> bytes:
    out = bytearray()
    code_at = 0
    out.append(0)                 # placeholder for the first code byte
    code = 1
    for byte in src:
        if byte != 0:
            out.append(byte)
            code += 1
        if byte == 0 or code == 0xFF:
            out[code_at] = code
            code_at = len(out)
            out.append(0)         # placeholder for the next code byte
            code = 1
    out[code_at] = code
    return bytes(out)


def cobs_decode(src: bytes) -> bytes | None:
    """None on a corrupt frame, never a partial result.

    A partial decode that looks like a short frame is how a corrupt frame becomes
    plausible data, so every failure returns None and the caller must check.
    """
    out = bytearray()
    i = 0
    n = len(src)
    while i < n:
        code = src[i]
        i += 1
        if code == 0:
            return None           # a zero cannot appear inside encoded data
        for _ in range(code - 1):
            if i >= n:
                return None       # the code promised more bytes than arrived
            out.append(src[i])
            i += 1
        if code != 0xFF and i < n:
            out.append(0)
    return bytes(out)


def encode(payload: bytes) -> bytes:
    """payload -> COBS(payload || crc) || delimiter."""
    crc = crc16(payload)
    body = payload + bytes((crc >> 8, crc & 0xFF))
    return cobs_encode(body) + bytes((DELIMITER,))


def decode(stuffed: bytes) -> tuple[str, bytes | None]:
    """What lay between two delimiters, to (verdict, payload).

    The verdict strings match the C result codes one for one, because the counts
    are different findings: a stuffing error means the receiver lost its place, a
    checksum error means the line corrupted a byte, and a reader who sees only a
    total cannot tell a noisy cable from a desynchronised receiver.
    """
    if len(stuffed) == 0:
        return ("empty", None)
    body = cobs_decode(stuffed)
    if body is None:
        return ("stuffing", None)
    if len(body) < CRC_BYTES + 1:
        return ("too_short", None)
    payload, got = body[:-CRC_BYTES], (body[-2] << 8) | body[-1]
    if crc16(payload) != got:
        return ("checksum", None)
    return ("ok", payload)


# ------------------------------------------------------------- the bit flipper

def flip_one_bit(frame: bytes, rng: random.Random) -> bytes:
    """Flip exactly one bit somewhere in the frame, delimiter excluded.

    The delimiter is excluded because flipping it tests the receiver's framing
    rather than the checksum, and those are different claims. A corrupted
    delimiter is the case that costs exactly one frame and then recovers, which is
    the property the format was chosen for and is tested separately.
    """
    body = bytearray(frame[:-1])
    if not body:
        return frame
    i = rng.randrange(len(body))
    body[i] ^= 1 << rng.randrange(8)
    return bytes(body) + frame[-1:]


def truncate(frame: bytes, rng: random.Random) -> bytes:
    """Cut the frame short, which is what a receiver sees when a byte is lost."""
    body = frame[:-1]
    if len(body) < 2:
        return frame
    return body[:rng.randrange(1, len(body))] + frame[-1:]


def inject_zero(frame: bytes, rng: random.Random) -> bytes:
    """Put a delimiter inside the frame, which must be impossible to confuse."""
    body = bytearray(frame[:-1])
    if not body:
        return frame
    body[rng.randrange(len(body))] = 0
    return bytes(body) + frame[-1:]
