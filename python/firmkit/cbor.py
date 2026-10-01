#!/usr/bin/env python3
"""Measure what the same five fields cost in CBOR, so the decision has numbers.

    cd python; python -m firmkit.cbor

Chapter 9's budget table carries the row "the same content as CBOR, about 17 B
by arithmetic, not measured". Step 10 of the chapter says to encode the same
fields with one of the three named libraries and put both numbers in the README.

None of those three libraries is installed here and none has a Python side that
would measure the C encoder's output anyway. What is available is the format
itself, which is specified precisely enough to encode exactly rather than
approximately: RFC 8949 fixes the head byte and the minimal-length integer
encoding, and nothing in this payload needs tags, floats, or indefinite lengths.

So this encodes the real bytes, with the canonical shortest-form rule, and counts
them. That is a measurement of the format, not of any one library. A library
would add its own framing choices and would not make the comparison fairer, only
less reproducible.

Three shapes are measured because the choice of shape dominates the result, and
quoting one number without saying which shape it was would be the same mistake
the chapter warns about elsewhere.
"""
from __future__ import annotations

import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent   # python/firmkit -> repo root
sys.path.insert(0, str(ROOT / "python" / "firmkit"))

import payload  # noqa: E402
from payload_fields import FIELDS  # noqa: E402


def head(major: int, value: int) -> bytes:
    """One CBOR head, in the canonical shortest form (RFC 8949 section 4.2.1)."""
    if value < 24:
        return bytes([(major << 5) | value])
    if value < 1 << 8:
        return bytes([(major << 5) | 24, value])
    if value < 1 << 16:
        return bytes([(major << 5) | 25]) + value.to_bytes(2, "big")
    if value < 1 << 32:
        return bytes([(major << 5) | 26]) + value.to_bytes(4, "big")
    return bytes([(major << 5) | 27]) + value.to_bytes(8, "big")


def cbor_int(n: int) -> bytes:
    """Major type 0 for non-negative, 1 for negative, as the format defines."""
    if n >= 0:
        return head(0, n)
    return head(1, -1 - n)


def cbor_text(s: str) -> bytes:
    raw = s.encode("utf-8")
    return head(3, len(raw)) + raw


def as_map_full_keys(fields: dict) -> bytes:
    out = head(5, len(FIELDS))
    for name, _, _, _ in FIELDS:
        out += cbor_text(name) + cbor_int(fields[name])
    return out


def as_map_short_keys(fields: dict) -> bytes:
    """One-letter keys, which is what anyone shipping CBOR on a slow link does."""
    short = {"version": "v", "flags": "f", "sequence": "s",
             "feature": "x", "battery": "b"}
    out = head(5, len(FIELDS))
    for name, _, _, _ in FIELDS:
        out += cbor_text(short[name]) + cbor_int(fields[name])
    return out


def as_array(fields: dict) -> bytes:
    """Positional, which gives up the format's main advantage: self-description."""
    out = head(4, len(FIELDS))
    for name, _, _, _ in FIELDS:
        out += cbor_int(fields[name])
    return out


SHAPES = (
    ("CBOR map, full field names", as_map_full_keys),
    ("CBOR map, one-letter keys", as_map_short_keys),
    ("CBOR array, positional", as_array),
)


def random_case(rng):
    out = {}
    for name, width, signed, _ in FIELDS:
        if signed:
            lo, hi = -(1 << (width - 1)), (1 << (width - 1)) - 1
        else:
            lo, hi = 0, (1 << width) - 1
        out[name] = rng.randint(lo, hi)
    return out


def main() -> int:
    with open(ROOT / "projects" / "P09-payload-codec" / "vectors.json", encoding="utf-8") as fh:
        vectors = json.load(fh)

    rng = random.Random(20260920)
    cases = [v["fields"] for v in vectors] + [random_case(rng) for _ in range(100000)]

    packed = len(payload.encode(cases[0]))
    print("Measured over the {} golden vectors and 100000 random cases,".format(len(vectors)))
    print("seed 20260920. Sizes in bytes, payload only, before framing.\n")
    print("  {:<28} {:>4} {:>4} {:>6}  {}".format("shape", "min", "max", "mean", "against packed"))
    print("  {:<28} {:>4} {:>4} {:>6}  {}".format("-" * 28, "----", "----", "------", "-" * 14))
    print("  {:<28} {:>4} {:>4} {:>6.2f}  {}".format(
        "bit-packed, this chapter", packed, packed, float(packed), "1.00x"))

    for label, fn in SHAPES:
        sizes = [len(fn(c)) for c in cases]
        lo, hi = min(sizes), max(sizes)
        mean = sum(sizes) / len(sizes)
        print("  {:<28} {:>4} {:>4} {:>6.2f}  {:.2f}x".format(
            label, lo, hi, mean, mean / packed))

    print("\nFramed, with chapter 5's one stuffing byte, two checksum bytes and")
    print("one delimiter, so four bytes of overhead on every row:")
    print("  {:<28} {:>4}".format("bit-packed", packed + 4))
    for label, fn in SHAPES:
        sizes = [len(fn(c)) for c in cases]
        mean = sum(sizes) / len(sizes)
        print("  {:<28} {:>6.2f}".format(label, mean + 4))

    print(
        "\nThe chapter's budget table estimated 'about 17 B by arithmetic'. The\n"
        "measured mean for the one-letter-key map is the closest shape to that\n"
        "estimate. The full-name map is far worse and the positional array is\n"
        "better, but the array gives up the only thing the format was chosen\n"
        "for, which is being readable without the header file. Quoting a single\n"
        "CBOR number without naming the shape would hide that."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
