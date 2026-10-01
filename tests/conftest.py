"""Shared fixtures for every suite. Central, and none of these touch hardware.

The import path deserves a word. `firmkit/payload.py` is copied verbatim to the
board under MicroPython, where there are no packages and no relative imports, so
it does a plain `import payload_fields` with a relative fallback. Both the
package form and the flat form therefore work, and `tests/test_micropython_subset.py`
asserts that the file stays inside the subset the board can load.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build-host"

sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "firmkit"))

P09 = ROOT / "projects" / "P09-payload-codec"
P06 = ROOT / "projects" / "P06-timer-sampling"


def shared_library_name(stem: str) -> str:
    return "{}.dll".format(stem) if sys.platform == "win32" else "lib{}.so".format(stem)


class CPayload(ctypes.Structure):
    """Must match payload_t in shared/payload.h, field for field and in order."""

    _fields_ = [
        ("version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("sequence", ctypes.c_uint32),
        ("feature", ctypes.c_int32),
        ("battery", ctypes.c_uint32),
    ]


def library_path() -> Path:
    return BUILD / shared_library_name("payload")


@pytest.fixture(scope="session")
def lib():
    """P09's C implementation, from the same source the board links."""
    path = library_path()
    if not path.exists():
        pytest.fail(
            "{} is missing. Build it first:\n"
            "    python tools/build_host.py".format(path.name)
        )
    dll = ctypes.CDLL(str(path))

    # Declaring these is not optional. ctypes defaults a return type to c_int,
    # which would truncate a size_t on a 64-bit host.
    dll.payload_encode.restype = ctypes.c_size_t
    dll.payload_encode.argtypes = [
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(CPayload)
    ]
    dll.payload_decode.restype = ctypes.c_int
    dll.payload_decode.argtypes = [
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(CPayload)
    ]
    return dll


@pytest.fixture(scope="session")
def vectors():
    """P09's hand-computed golden vectors. No generator has touched that file."""
    with open(P09 / "vectors.json", encoding="utf-8") as fh:
        return json.load(fh)


def c_encode(lib, fields: dict) -> bytes:
    buf = (ctypes.c_uint8 * 8)()
    n = lib.payload_encode(buf, 8, ctypes.byref(CPayload(**fields)))
    assert n == 5, "the encoder returned {} for {}".format(n, fields)
    return bytes(buf[:5])


def c_decode(lib, data: bytes) -> dict:
    out = CPayload()
    raw = (ctypes.c_uint8 * len(data))(*data)
    rc = lib.payload_decode(raw, len(data), ctypes.byref(out))
    assert rc == 0, "the decoder returned {} for {}".format(rc, data.hex())
    return {
        "version": out.version,
        "flags": out.flags,
        "sequence": out.sequence,
        "feature": out.feature,
        "battery": out.battery,
    }
