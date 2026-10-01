"""The C++ variant against the C one, over the vectors and the whole random run.

The acceptance criterion in chapter 9 is that the C++ variant produces
byte-identical output to the C variant for every vector and for the whole random
run, and that both size outputs are published rather than one being asserted as
smaller.

The variant is driven as a filter rather than as a C++ test that parses
vectors.json, which is what the chapter sketched. Parsing JSON in C++ with no
dependency is a lot of code that tests nothing, and it would have limited the
comparison to six cases. This way the same 100000 cases go through all three
implementations.
"""
from __future__ import annotations

import os
import random
import subprocess
from pathlib import Path

from firmkit import payload
import pytest
from conftest import BUILD, ROOT
from test_roundtrip import SEED, c_encode_cached, random_case

FILTER = BUILD / ("cpp_filter.exe" if os.name == "nt" else "cpp_filter")
CASES = int(os.environ.get("CODEC_CASES", "100000"))


def run_filter(cases):
    """Send every case through the C++ filter in one process.

    One process for the whole run rather than one per case: 100000 process
    launches would dominate the test time and measure the operating system
    instead of the codec.
    """
    if not FILTER.exists():
        pytest.skip(
            "{} is missing. Build it with: python tools/build_host.py".format(
                FILTER.name
            )
        )
    stdin = "".join(
        "{} {} {} {} {}\n".format(
            c["version"], c["flags"], c["sequence"], c["feature"], c["battery"]
        )
        for c in cases
    )
    proc = subprocess.run(
        [str(FILTER)], input=stdin, capture_output=True, text=True, check=True
    )
    out = []
    for line in proc.stdout.splitlines():
        parts = line.split()
        assert len(parts) == 6, "unexpected filter output: {!r}".format(line)
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
    assert len(out) == len(cases), "the filter returned {} lines for {} cases".format(
        len(out), len(cases)
    )
    return out


def test_cpp_matches_the_vectors(vectors):
    got = run_filter([v["fields"] for v in vectors])
    for v, (hex_bytes, decoded) in zip(vectors, got):
        assert hex_bytes == v["bytes"], "{}: C++ encoded {} expected {}".format(
            v["name"], hex_bytes, v["bytes"]
        )
        assert decoded == v["fields"], "{}: C++ decoded {} expected {}".format(
            v["name"], decoded, v["fields"]
        )


def test_cpp_matches_c_over_the_whole_random_run():
    """All three implementations, byte for byte, over the recorded-seed run."""
    rng = random.Random(SEED)
    cases = [random_case(rng) for _ in range(CASES)]
    got = run_filter(cases)
    for i, (case, (hex_bytes, decoded)) in enumerate(zip(cases, got)):
        from_c = c_encode_cached(case).hex().upper()
        from_py = payload.encode(case).hex().upper()
        assert hex_bytes == from_c, (
            "case {}: C++ gave {} and C gave {} for {}".format(i, hex_bytes, from_c, case)
        )
        assert hex_bytes == from_py, (
            "case {}: C++ gave {} and Python gave {} for {}".format(
                i, hex_bytes, from_py, case
            )
        )
        assert decoded == case, "case {}: C++ decoded {} expected {}".format(
            i, decoded, case
        )


def test_the_compile_time_self_test_is_present():
    """payload.hpp asserts the third vector at compile time.

    If that static_assert block were deleted, this repository would lose the one
    property the C version cannot have, and no other test would notice. So the
    presence of it is asserted here, which is a weaker check than the assertion
    itself but catches its removal.
    """
    text = (ROOT / "cpp" / "P09" / "payload.hpp").read_text(encoding="utf-8")
    assert "static_assert(third_bytes[0] == 0x24" in text
    assert "sign extension at compile time" in text
