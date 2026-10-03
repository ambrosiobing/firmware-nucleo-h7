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

ROOT = Path(__file__).resolve().parent.parent.parent   # python/tests -> repo root
BUILD = ROOT / "build-host"

sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(ROOT / "python" / "firmkit"))

P09 = ROOT / "projects" / "P09-payload-codec"   # the specification and the vectors
P06 = ROOT / "projects" / "P06-timer-sampling"


def shared_library_name(stem: str) -> str:
    return "{}.dll".format(stem) if sys.platform == "win32" else "lib{}.so".format(stem)


def executable_name(stem: str) -> str:
    return "{}.exe".format(stem) if sys.platform == "win32" else stem


# The two implementations that are driven as filters rather than through
# ctypes, and the command that builds each. Both speak one protocol: five
# integers per line on stdin, and on stdout the five encoded bytes as uppercase
# hex followed by the round trip of those bytes back into fields. That is what
# lets one helper drive both, and it is why neither of them parses vectors.json.
CPP_FILTER = BUILD / executable_name("cpp_filter")
RUST_FILTER = ROOT / "target" / "release" / executable_name("p09-filter")

FILTER_BUILD_COMMAND = {
    CPP_FILTER: "python python/tools/build_host.py",
    RUST_FILTER: "cargo build --release --workspace",
}


def run_codec_filter(path: Path, cases, required: bool = False):
    """Send every case through one filter process and parse what comes back.

    One process for the whole run rather than one per case: a hundred thousand
    process launches would dominate the test time and measure the operating
    system instead of the codec.

    Skips, with the build command named, when the filter has not been built.
    `required=True` turns that into a failure instead, for the one test whose
    whole purpose is to notice that nothing was compared.
    """
    import subprocess

    if not path.exists():
        message = "{} is missing. Build it with:\n    {}".format(
            path.name, FILTER_BUILD_COMMAND.get(path, "see the project README")
        )
        if required:
            pytest.fail(message)
        pytest.skip(message)

    stdin = "".join(
        "{} {} {} {} {}\n".format(
            c["version"], c["flags"], c["sequence"], c["feature"], c["battery"]
        )
        for c in cases
    )
    proc = subprocess.run(
        [str(path)], input=stdin, capture_output=True, text=True, check=True
    )
    out = []
    for line in proc.stdout.splitlines():
        parts = line.split()
        assert len(parts) == 6, "unexpected output from {}: {!r}".format(
            path.name, line
        )
        out.append(
            (
                parts[0],
                {
                    "version": int(parts[1]),
                    "flags": int(parts[2]),
                    "sequence": int(parts[3]),
                    "feature": int(parts[4]),
                    "battery": int(parts[5]),
                },
            )
        )
    assert len(out) == len(cases), "{} returned {} lines for {} cases".format(
        path.name, len(out), len(cases)
    )
    return out


class CPayload(ctypes.Structure):
    """Must match payload_t in c/payload/payload.h, field for field and in order."""

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
            "    python python/tools/build_host.py".format(path.name)
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
