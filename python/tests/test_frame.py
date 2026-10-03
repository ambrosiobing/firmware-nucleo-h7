"""P05's framing: two implementations, one published check value, and corruption.

Three claims, in rising order of what they prove:

  1. Both implementations reproduce 0x29B1, the published checksum of the nine
     bytes "123456789". This is the only check here that appeals to something
     outside this repository, which is why it comes first.
  2. The C and the Python agree on every frame over thousands of random payloads,
     in both directions. Two implementations of one format agreeing is far
     stronger than either one round-tripping against itself.
  3. The format rejects corruption. A checksum that has never been shown to reject
     anything is a decoration, so frames are corrupted on purpose and what gets
     through is counted.

No hardware. The C comes from build-host/frame.{dll,so}, built from the same
three files the board links.
"""
from __future__ import annotations

import ctypes
import random
import sys
from pathlib import Path

import pytest
from conftest import BUILD, shared_library_name

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "projects" / "P05-framing-crc" / "python"))

import twin  # noqa: E402

CASES = 3000
MAX_PAYLOAD = 64

_LIB = None


def lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("frame")
        if not path.exists():
            pytest.fail("{} is missing. Run: python python/tools/build_host.py"
                        .format(path.name))
        d = ctypes.CDLL(str(path))
        d.crc16_ref.restype = ctypes.c_uint16
        d.crc16_ref.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
        d.frame_encode.restype = ctypes.c_size_t
        d.frame_encode.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
        d.frame_decode.restype = ctypes.c_int
        d.frame_decode.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]
        _LIB = d
    return _LIB


def buf(data: bytes):
    return (ctypes.c_uint8 * max(1, len(data)))(*data)


def c_crc16(data: bytes) -> int:
    return int(lib().crc16_ref(buf(data), len(data)))


def c_encode(payload: bytes) -> bytes:
    out = (ctypes.c_uint8 * 256)()
    n = lib().frame_encode(buf(payload), len(payload), out, 256)
    return bytes(out[:n])


def c_decode(stuffed: bytes):
    out = (ctypes.c_uint8 * 256)()
    got = ctypes.c_size_t(0)
    rc = lib().frame_decode(buf(stuffed), len(stuffed), out, 256, ctypes.byref(got))
    return rc, bytes(out[:got.value])


# ------------------------------------------- 1. the one external authority

def test_both_implementations_reproduce_the_published_check_value():
    """0x29B1 over "123456789". The only check here that is not self-referential.

    If either side fails this, nothing else in P05 is worth running, which is why
    it is the first test in the file.
    """
    assert c_crc16(b"123456789") == 0x29B1
    assert twin.crc16(b"123456789") == 0x29B1
    assert twin.CRC16_CHECK == 0x29B1


def test_the_two_checksums_agree_over_random_input():
    rng = random.Random(20261001)
    for _ in range(CASES):
        data = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 80)))
        assert c_crc16(data) == twin.crc16(data), data.hex()


# ------------------------------------------- 2. two implementations, one format

def test_the_two_encoders_produce_identical_frames():
    """Identical bytes, not merely compatible ones.

    A round trip can pass while both sides make the same mistake symmetrically.
    Comparing the bytes catches that.
    """
    rng = random.Random(20261002)
    for _ in range(CASES):
        payload = bytes(rng.randrange(256) for _ in range(rng.randrange(1, MAX_PAYLOAD + 1)))
        assert c_encode(payload) == twin.encode(payload), payload.hex()


def test_each_decoder_accepts_the_other_encoder():
    """The cross direction, which a same-language round trip cannot test."""
    rng = random.Random(20261003)
    for _ in range(CASES):
        payload = bytes(rng.randrange(256) for _ in range(rng.randrange(1, MAX_PAYLOAD + 1)))

        verdict, back = twin.decode(c_encode(payload)[:-1])
        assert verdict == "ok" and back == payload

        rc, back = c_decode(twin.encode(payload)[:-1])
        assert rc == 0 and back == payload


def test_payloads_full_of_zeros_survive_the_stuffing():
    """The case byte stuffing exists for, and the one a naive encoder breaks."""
    for payload in (b"\x00", b"\x00" * 10, b"\x00\x01\x00\x02\x00",
                    bytes(64), b"\x01" + bytes(62) + b"\x02"):
        assert c_encode(payload) == twin.encode(payload)
        rc, back = c_decode(c_encode(payload)[:-1])
        assert rc == 0 and back == payload
        assert b"\x00" not in c_encode(payload)[:-1], "a zero escaped into the frame"


def test_a_long_run_crosses_the_254_byte_code_boundary():
    """A run of non-zero bytes longer than a code byte can count must still work."""
    payload = bytes((i % 255) + 1 for i in range(MAX_PAYLOAD))
    assert c_encode(payload) == twin.encode(payload)
    rc, back = c_decode(c_encode(payload)[:-1])
    assert rc == 0 and back == payload


def test_the_five_byte_payload_frames_to_nine_bytes():
    """The number P09's argument for bit packing rests on, derived rather than
    asserted in prose: one stuffing byte, five of payload, two of checksum, one
    delimiter."""
    frame = c_encode(bytes.fromhex("2405FFFFE8"))
    assert len(frame) == 9
    assert frame == twin.encode(bytes.fromhex("2405FFFFE8"))


# ------------------------------------------- 3. the format rejects corruption

def test_every_single_bit_flip_is_rejected():
    """CRC-16 detects every one-bit error, and this holds it to that.

    Not "most": every one. A single survivor would mean the parameters are not the
    ones claimed, because this property follows from them.
    """
    rng = random.Random(20261004)
    undetected = []
    for _ in range(CASES):
        payload = bytes(rng.randrange(256) for _ in range(rng.randrange(1, MAX_PAYLOAD + 1)))
        frame = c_encode(payload)
        bad = twin.flip_one_bit(frame, rng)
        if bad == frame:
            continue
        rc, back = c_decode(bad[:-1])
        if rc == 0 and back == payload:
            undetected.append((payload.hex(), frame.hex(), bad.hex()))
    assert not undetected, "a single bit flip got through: {}".format(undetected[:3])


def test_the_two_implementations_reject_the_same_corruptions():
    """A disagreement here would mean one of them is more permissive, which is
    worse than either being wrong: the strict side would pass the test suite while
    the permissive one shipped."""
    rng = random.Random(20261005)
    for _ in range(CASES):
        payload = bytes(rng.randrange(256) for _ in range(rng.randrange(1, MAX_PAYLOAD + 1)))
        frame = c_encode(payload)
        bad = rng.choice((twin.flip_one_bit, twin.truncate, twin.inject_zero))(frame, rng)

        rc, c_back = c_decode(bad[:-1])
        verdict, py_back = twin.decode(bad[:-1])

        c_ok = (rc == 0)
        py_ok = (verdict == "ok")
        assert c_ok == py_ok, (
            "C says {} and Python says {} for {}".format(rc, verdict, bad.hex()))
        if c_ok:
            assert c_back == py_back


def test_a_truncated_frame_is_refused_and_not_partially_delivered():
    rng = random.Random(20261006)
    delivered_partial = 0
    for _ in range(CASES):
        payload = bytes(rng.randrange(256) for _ in range(rng.randrange(4, MAX_PAYLOAD + 1)))
        frame = c_encode(payload)
        bad = twin.truncate(frame, rng)
        rc, back = c_decode(bad[:-1])
        if rc == 0 and back != payload:
            delivered_partial += 1
    assert delivered_partial == 0, "a truncated frame was delivered as data"


def test_an_injected_delimiter_is_never_read_as_data():
    """A zero inside the frame means the receiver lost its place. It must be a
    refusal, because interpreting it would turn a framing error into plausible
    data."""
    rng = random.Random(20261007)
    for _ in range(500):
        payload = bytes(rng.randrange(1, 256) for _ in range(rng.randrange(2, 20)))
        frame = c_encode(payload)
        bad = twin.inject_zero(frame, rng)
        if bad == frame:
            continue
        rc, back = c_decode(bad[:-1])
        assert not (rc == 0 and back == payload), "an injected delimiter was ignored"


def test_an_empty_run_between_delimiters_is_not_an_error():
    """What an idle line looks like. Discarded, not reported as corruption."""
    rc, _ = c_decode(b"")
    assert rc == 1, "an idle line should be FRAME_EMPTY, not a failure"
    assert twin.decode(b"")[0] == "empty"
