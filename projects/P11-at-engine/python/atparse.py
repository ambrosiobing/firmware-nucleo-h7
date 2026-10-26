"""P11's classifier, deadline test and backoff in Python, the twin of atparse.c.

The same two tables, the same prefix matching, the same signed-difference
deadline over a 32 bit counter and the same scatter. Python has no 32 bit
integers, so the wrap is done explicitly here and that is the one place the two
implementations are written differently for the same behaviour.
"""
from __future__ import annotations

FINAL_OK = 0
FINAL_BAD = 1
URC = 2
INTERMEDIATE = 3
EMPTY = 4

CLASS_NAMES = ("final_ok", "final_bad", "urc", "intermediate", "empty")

BACKOFF_REFUSED = 0xFFFFFFFF

_FINAL_OK = ("OK", "CONNECT")
_FINAL_BAD = ("ERROR", "+CME ERROR:", "+CMS ERROR:", "NO CARRIER", "ABORTED")


def classify(line, urcs=None) -> int:
    if line is None or line == "":
        return EMPTY
    if any(line.startswith(p) for p in _FINAL_OK):
        return FINAL_OK
    if any(line.startswith(p) for p in _FINAL_BAD):
        return FINAL_BAD
    if urcs and any(line.startswith(p) for p in urcs):
        return URC
    return INTERMEDIATE


def due(now_ms: int, deadline_ms: int) -> bool:
    """The signed difference, with the 32 bit wrap written out.

    The C gets the wrap from the type. Python has to be told, which is why this
    masks to 32 bits and then reads the top bit as a sign.
    """
    diff = (now_ms - deadline_ms) & 0xFFFFFFFF
    if diff >= 0x80000000:
        diff -= 0x100000000
    return diff >= 0


def backoff_ms(attempt: int, base_ms: int, cap_ms: int, rand_draw: int) -> int:
    if base_ms == 0 or cap_ms < base_ms:
        return BACKOFF_REFUSED
    if cap_ms > 0xFFFFFFFF // 2:
        return BACKOFF_REFUSED

    d = base_ms
    for _ in range(attempt):
        if d >= cap_ms:
            break
        d = (d << 1) & 0xFFFFFFFF
    if d > cap_ms:
        d = cap_ms

    span = (d >> 2) if (d >> 2) != 0 else 1
    return d - (d >> 3) + (rand_draw % span)


def class_name(value: int) -> str:
    if 0 <= value < len(CLASS_NAMES):
        return CLASS_NAMES[value]
    return "invalid"
