"""P04's span arithmetic in Python, the twin of c/rxspan.c.

The same three cases, the same masked advance, the same refusals and the same
floor on the size. It exists so the oracle is driven by two independent
implementations rather than by the one it was written from.
"""
from __future__ import annotations

RXSPAN_MAX = 2
RXSPAN_MIN_SIZE = 8

ERR_ARGUMENT = -1
ERR_SIZE = -2
ERR_POSITION = -3

ERRORS = {"argument": ERR_ARGUMENT, "size": ERR_SIZE, "position": ERR_POSITION}


def next_spans(size: int, old_pos: int, pos: int, stats: dict):
    """Return (count, spans, stats) where count may be a negative refusal.

    stats is mutated in place, as the C does, and carries peak_advance,
    near_lap, calls and deliveries.
    """
    if stats is None:
        return ERR_ARGUMENT, []

    if size < RXSPAN_MIN_SIZE or (size & (size - 1)) != 0:
        return ERR_SIZE, []
    if not (0 <= old_pos < size) or not (0 <= pos < size):
        return ERR_POSITION, []

    # Masked, not subtracted, which is why the size must be a power of two.
    advance = (pos - old_pos) & (size - 1)

    stats["calls"] += 1
    if advance > stats["peak_advance"]:
        stats["peak_advance"] = advance
    if advance > size // 2:
        stats["near_lap"] += 1

    if pos == old_pos:
        return 0, []

    if pos > old_pos:
        spans = [(old_pos, pos - old_pos)]
    else:
        spans = [(old_pos, size - old_pos)]
        if pos > 0:
            spans.append((0, pos))

    stats["deliveries"] += len(spans)
    return len(spans), spans


def fresh_stats() -> dict:
    return {"peak_advance": 0, "near_lap": 0, "calls": 0, "deliveries": 0}
