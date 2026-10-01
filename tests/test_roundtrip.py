"""The round-trip property over 100000 random cases, with a recorded seed.

The property: for every assignment of in-range values to the five fields, the C
encoder produces five bytes which the Python decoder turns back into exactly
those values.

The seed is fixed and recorded on purpose. A test whose input changes on every
run finds more defects and reports them in a form nobody can act on. The
compromise is this fixed-seed test plus test_roundtrip_random_seed below, which
runs a smaller number of cases with a seed it prints, so a failure there is
still reproducible by reading the output.
"""
from __future__ import annotations

import os
import random

from firmkit import payload
from conftest import c_decode, c_encode
from firmkit.payload_fields import FIELDS

SEED = 20260920
CASES = int(os.environ.get("CODEC_CASES", "100000"))


def random_case(rng):
    """One case, every field uniform over its full inclusive range."""
    out = {}
    for name, width, signed, _ in FIELDS:
        if signed:
            lo, hi = -(1 << (width - 1)), (1 << (width - 1)) - 1
        else:
            lo, hi = 0, (1 << width) - 1
        out[name] = rng.randint(lo, hi)
    return out


def test_roundtrip_c_encode_python_decode():
    """The headline property: C encodes, Python decodes, nothing is lost."""
    rng = random.Random(SEED)
    for i in range(CASES):
        case = random_case(rng)
        got = payload.decode(c_encode_cached(case))
        assert got == case, "case {} with seed {}: {} became {}".format(
            i, SEED, case, got
        )


def test_roundtrip_python_encode_c_decode():
    """The other direction, which catches an asymmetry the first one cannot."""
    rng = random.Random(SEED + 1)
    for i in range(CASES):
        case = random_case(rng)
        got = _c_decode(payload.encode(case))
        assert got == case, "case {} with seed {}: {} became {}".format(
            i, SEED + 1, case, got
        )


def test_the_two_encoders_agree_byte_for_byte():
    """C and Python must produce identical bytes, not merely compatible ones.

    A round trip can pass while the two sides disagree, if both make the same
    mistake symmetrically. Comparing the bytes catches that.
    """
    rng = random.Random(SEED + 2)
    for i in range(CASES):
        case = random_case(rng)
        a, b = c_encode_cached(case), payload.encode(case)
        assert a == b, "case {}: C gave {} and Python gave {} for {}".format(
            i, a.hex().upper(), b.hex().upper(), case
        )


def test_roundtrip_random_seed():
    """A smaller run with a seed chosen at random and printed.

    This is the job that finds what the fixed seed never will. It prints the
    seed before doing any work, so a failure in continuous integration is
    reproducible from the log by setting SEED to the printed value.
    """
    seed = random.SystemRandom().randrange(1 << 32)
    print("random-seed run, seed = {}".format(seed))
    rng = random.Random(seed)
    for i in range(2000):
        case = random_case(rng)
        got = payload.decode(c_encode_cached(case))
        assert got == case, "case {} with seed {}: {} became {}".format(
            i, seed, case, got
        )


def test_out_of_range_values_are_masked_not_rejected():
    """Documented behaviour, asserted rather than assumed.

    A sequence counter that wraps at 512 is meant to wrap. Both sides mask to
    the field width, and both must do it the same way, which is the part worth
    testing: a mask on one side and a rejection on the other would pass every
    in-range test.
    """
    for name, width, signed, _ in FIELDS:
        base = {n: 0 for n, _, _, _ in FIELDS}
        over = dict(base)
        over[name] = (1 << width) + 1          # one past a full wrap
        wrapped = dict(base)
        wrapped[name] = 1
        assert c_encode_cached(over) == c_encode_cached(wrapped), name
        assert payload.encode(over) == payload.encode(wrapped), name


# --------------------------------------------------------------------------
# The fixtures are session scoped, but these tests are called often enough that
# going through the fixture machinery per case costs more than the test. The
# library is loaded once here and the two helpers close over it.

_LIB = None


def _load():
    global _LIB
    if _LIB is None:
        import ctypes

        from conftest import CPayload, library_path

        path = library_path()
        assert path.exists(), "{} is missing. Run: python tools/build_host.py".format(
            path.name
        )
        dll = ctypes.CDLL(str(path))
        dll.payload_encode.restype = ctypes.c_size_t
        dll.payload_encode.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(CPayload)
        ]
        dll.payload_decode.restype = ctypes.c_int
        dll.payload_decode.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(CPayload)
        ]
        _LIB = dll
    return _LIB


def c_encode_cached(fields):
    return c_encode(_load(), fields)


def _c_decode(data):
    return c_decode(_load(), data)
