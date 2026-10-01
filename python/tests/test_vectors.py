"""Both implementations against the hand-computed vectors.

This is the test that matters most and it is the shortest. The round-trip test
in test_roundtrip.py proves the two implementations agree with each other; only
these vectors prove they agree with the specification. Two outputs of one
generator agreeing proves that the generator is self-consistent and nothing
more.

If an implementation disagrees with a vector here, the implementation is wrong.
The vector is re-derived on paper from docs/bitorder.md, never adjusted to match
whatever an encoder printed.
"""
from __future__ import annotations

from firmkit import payload
from conftest import c_decode, c_encode


def test_c_encoder_matches_vectors(lib, vectors):
    for v in vectors:
        got = c_encode(lib, v["fields"]).hex().upper()
        assert got == v["bytes"], "{}: C encoded {} expected {}".format(
            v["name"], got, v["bytes"]
        )


def test_c_decoder_matches_vectors(lib, vectors):
    for v in vectors:
        got = c_decode(lib, bytes.fromhex(v["bytes"]))
        assert got == v["fields"], "{}: C decoded {} expected {}".format(
            v["name"], got, v["fields"]
        )


def test_python_encoder_matches_vectors(vectors):
    for v in vectors:
        got = payload.encode(v["fields"]).hex().upper()
        assert got == v["bytes"], "{}: Python encoded {} expected {}".format(
            v["name"], got, v["bytes"]
        )


def test_python_decoder_matches_vectors(vectors):
    for v in vectors:
        got = payload.decode(bytes.fromhex(v["bytes"]))
        assert got == v["fields"], "{}: Python decoded {} expected {}".format(
            v["name"], got, v["fields"]
        )


def test_the_two_maximum_cases_are_different(vectors):
    """The published chapter conflated these two, so the test says so.

    Chapter 9 prints its second vector with feature 131071 and bytes
    FFFFFFFFFF. Those contradict each other: 131071 is the largest positive
    value of an 18-bit two's complement field, so its top bit is zero and
    byte 2 is 7F. FFFFFFFFFF decodes to feature -1.

    Both cases are real and both are in vectors.json. This test exists to stop
    anyone merging them back together.
    """
    by_bytes = {v["bytes"]: v["fields"] for v in vectors}
    assert "FFFF7FFFFF" in by_bytes, "the maximum positive feature case is missing"
    assert "FFFFFFFFFF" in by_bytes, "the all-bits-set case is missing"
    assert by_bytes["FFFF7FFFFF"]["feature"] == 131071
    assert by_bytes["FFFFFFFFFF"]["feature"] == -1


def test_decoder_rejects_a_wrong_length(lib):
    """A short or long buffer is refused rather than read past or padded."""
    import ctypes

    from conftest import CPayload

    out = CPayload()
    for n in (0, 1, 4, 6, 8):
        raw = (ctypes.c_uint8 * n)(*([0] * n))
        rc = lib.payload_decode(raw, n, ctypes.byref(out))
        assert rc == -1, "the C decoder accepted {} bytes".format(n)

    for n in (0, 1, 4, 6, 8):
        try:
            payload.decode(bytes(n))
        except ValueError:
            pass
        else:
            raise AssertionError("the Python decoder accepted {} bytes".format(n))


def test_encoder_does_not_write_past_its_five_bytes(lib, vectors):
    """Pre-dirty the buffer, then check the tail is untouched.

    This exists because of a mutation experiment on Thursday 1 October 2026.
    Deleting the memset from payload_encode did not fail any test, since bw_put
    writes every one of the forty bits explicitly, clearing as well as setting.
    The memset is a second layer of defence against a specification whose widths
    do not sum to the byte count, which the static assertions already prevent, so
    no test can observe it. That is worth knowing rather than patching over, and
    the memset stays.

    What is observable, and was not tested before, is the boundary: a caller that
    passes a larger buffer must get its tail back unchanged.
    """
    import ctypes

    from conftest import CPayload

    for v in vectors:
        buf = (ctypes.c_uint8 * 8)(*([0xAA] * 8))
        n = lib.payload_encode(buf, 8, ctypes.byref(CPayload(**v["fields"])))
        assert n == 5
        assert bytes(buf[:5]).hex().upper() == v["bytes"], v["name"]
        assert bytes(buf[5:]) == b"\xAA\xAA\xAA", (
            "{}: the encoder wrote past PAYLOAD_BYTES".format(v["name"])
        )


def test_encoder_refuses_too_small_a_buffer(lib):
    import ctypes

    from conftest import CPayload

    case = CPayload(version=1, flags=0, sequence=0, feature=0, battery=0)
    for cap in (0, 1, 4):
        buf = (ctypes.c_uint8 * 8)()
        n = lib.payload_encode(buf, cap, ctypes.byref(case))
        assert n == 0, "the encoder wrote {} bytes into a buffer of {}".format(n, cap)
