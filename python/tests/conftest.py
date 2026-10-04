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


# The implementations that are driven as filters rather than through ctypes,
# and the command that builds each. The C++ and Rust of one project speak one
# protocol, so one helper drives both, and neither parses the project's oracle:
# that stays in the test, which owns it.
#
#   P09  five integers per line in; the five encoded bytes as uppercase hex and
#        the round trip of those bytes back into fields out
#   P05  "C hex", "E hex" or "D hex" per line in; the checksum, the frame, or a
#        verdict and the payload out
CPP_FILTER = BUILD / executable_name("cpp_filter")
RUST_FILTER = ROOT / "target" / "release" / executable_name("p09-filter")
FRAME_CPP_FILTER = BUILD / executable_name("frame_filter")
FRAME_RUST_FILTER = ROOT / "target" / "release" / executable_name("p05-filter")
SM_CPP_FILTER = BUILD / executable_name("node_sm_filter")
SM_RUST_FILTER = ROOT / "target" / "release" / executable_name("p08-filter")
RING_CPP_FILTER = BUILD / executable_name("ring_filter")
RING_RUST_FILTER = ROOT / "target" / "release" / executable_name("p02-filter")
ATTR_CPP_FILTER = BUILD / executable_name("attribute_filter")
ATTR_RUST_FILTER = ROOT / "target" / "release" / executable_name("p03-filter")
RATE_CPP_FILTER = BUILD / executable_name("rate_filter")
RATE_RUST_FILTER = ROOT / "target" / "release" / executable_name("p06-filter")
GATES_CPP_FILTER = BUILD / executable_name("gates_filter")
GATES_RUST_FILTER = ROOT / "target" / "release" / executable_name("p12-filter")
CLOCK_CPP_FILTER = BUILD / executable_name("clocktree_filter")
CLOCK_RUST_FILTER = ROOT / "target" / "release" / executable_name("p01-filter")

#   P08  a sample and then event names per line in; the final state, every row
#        index taken, the counters and the stub's frame out
#   P03  A with five counters, F or R per line in; the nine attributed fields,
#        the first-loss index, or an acknowledged reset out
#   P06  A with a capture's PATH, the sample rate and the nominal rate in; the
#        witness's twenty fields out. A path and not the samples, because a
#        capture is thousands of doubles and a long line is what broke P08's
#        filter, and because a path is how rate.py receives one
#   P02  one operation per line in, I, P, G or S; the answer to that one
#        operation out. Short lines and many of them, the opposite of P08, since
#        P02's comparison runs to hundreds of thousands of operations
#   P01  "D" and seven register words and two input frequencies, or "B" and a
#        reported and a true frequency, per line in; four frequencies and a
#        refusal token, or an ok flag and three signed parts-per-million
#        figures, out. Decimal throughout, including the registers, so that no
#        implementation needs a second parser
FILTER_BUILD_COMMAND = {
    CPP_FILTER: "python python/tools/build_host.py",
    FRAME_CPP_FILTER: "python python/tools/build_host.py",
    SM_CPP_FILTER: "python python/tools/build_host.py",
    RUST_FILTER: "cargo build --release --workspace",
    FRAME_RUST_FILTER: "cargo build --release --workspace",
    SM_RUST_FILTER: "cargo build --release --workspace",
    RING_CPP_FILTER: "python python/tools/build_host.py",
    RING_RUST_FILTER: "cargo build --release --workspace",
    ATTR_CPP_FILTER: "python python/tools/build_host.py",
    ATTR_RUST_FILTER: "cargo build --release --workspace",
    RATE_CPP_FILTER: "python python/tools/build_host.py",
    RATE_RUST_FILTER: "cargo build --release --workspace",
    GATES_CPP_FILTER: "python python/tools/build_host.py",
    GATES_RUST_FILTER: "cargo build --release --workspace",
    CLOCK_CPP_FILTER: "python python/tools/build_host.py",
    CLOCK_RUST_FILTER: "cargo build --release --workspace",
}


def run_filter(path: Path, lines, required: bool = False):
    """Send every request line through one filter process; return its answers.

    One process for the whole run rather than one per request: a hundred
    thousand process launches would dominate the test time and measure the
    operating system instead of the code under test. Exactly one answer per
    request is asserted, so a filter that drops or doubles a line is reported
    as that rather than as a mismatch further down.

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

    lines = list(lines)
    proc = subprocess.run(
        [str(path)],
        input="".join(line + "\n" for line in lines),
        capture_output=True,
        text=True,
        check=True,
    )
    answers = proc.stdout.splitlines()
    assert len(answers) == len(lines), "{} returned {} lines for {} requests".format(
        path.name, len(answers), len(lines)
    )
    return answers


def assert_not_vacuous(present, project: str, minimum: int = 3):
    """At least `minimum` of the four languages must actually have been compared.

    A parity suite that passes because nothing was built has proven nothing, and
    that is the one failure such a suite cannot notice about itself. So it is
    asserted rather than assumed.

    The Windows branch is not a softening of the bar. Two of the four reach the
    comparison through a compiler, and compiling on win11 aquamarine is
    forbidden: builds happen in WSL on the win11 skyhorizon demo laptop,
    bing@JPTOUPM678, and on the CI runner. So on Windows the honest result is a
    skip that names the command, exactly as `test_ring.py` does for the ring
    assertion, rather than a failure that points at the code or a pass that
    pretends four were compared. In WSL and in CI the bar is the full one.
    """
    if len(present) >= minimum:
        return
    names = ", ".join(sorted(present)) or "none"
    if sys.platform.startswith("win"):
        pytest.skip(
            "{} compared {} of four languages here ({}). The other filters need a\n"
            "compiler, which this laptop does not run. In WSL on bing@JPTOUPM678:\n"
            "    python3 python/tools/build_host.py\n"
            "    cargo build --release --workspace".format(project, len(present), names)
        )
    pytest.fail(
        "{} compared only {} of the four languages: {}. A parity test that passes "
        "because nothing was built has proven nothing.".format(project, len(present), names)
    )


def run_codec_filter(path: Path, cases, required: bool = False):
    """P09's protocol over `run_filter`: cases in, (hex, decoded fields) out."""
    answers = run_filter(
        path,
        (
            "{} {} {} {} {}".format(
                c["version"], c["flags"], c["sequence"], c["feature"], c["battery"]
            )
            for c in cases
        ),
        required=required,
    )
    out = []
    for line in answers:
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
