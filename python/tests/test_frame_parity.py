"""P05's four implementations against one oracle, byte for byte and verdict for
verdict.

The oracle is the project's, not any language's, and it has three parts:

  1. The published check value. CRC-16 with polynomial 0x1021, initial 0xFFFF,
     no reflection and no final exclusive-or gives 0x29B1 over the nine bytes
     "123456789". Every catalogue lists it. It is the only check here that
     appeals to something outside this repository, which is why it comes first.
  2. Identical frames. Not compatible ones: a round trip passes while both sides
     make the same mistake symmetrically, and comparing the bytes catches that.
  3. Identical verdicts on corruption. A frame format exists to notice when the
     line has corrupted something, so frames are corrupted on purpose three ways
     and every implementation must refuse the same ones with the same name. A
     disagreement would mean one is more permissive, which is worse than either
     being wrong: the strict one passes the suite while the permissive one ships.

C is the reference, for the same reason as in P09: it is the implementation the
board will link. `python/tests/test_frame.py` already holds C and Python to all
three parts; this file adds C++ and Rust and makes it one comparison rather than
four, over one case list, so a disagreement is reported as a disagreement.

**One divergence found by writing this, pinned rather than hidden.** The C's
`cobs_decode` returns 0 for both a corrupt frame and a frame that decodes to zero
bytes, which the single byte 0x01 does, legitimately. So the C names that frame a
stuffing error where the format says it is too short to carry its checksum. The
Python twin, the C++ and the Rust all say too_short, because each can tell empty
from failed. Both answers are refusals, so the chapter's claim holds; the name
does not, for exactly one input, and `test_the_lone_code_byte_is_where_c_names_
the_refusal_differently` asserts that state so that fixing the C makes a test
change rather than a silent one.
"""
from __future__ import annotations

import random
import sys
from pathlib import Path

import pytest

from conftest import (
    FRAME_CPP_FILTER,
    FRAME_RUST_FILTER,
    assert_not_vacuous,
    run_filter,
)

sys.path.insert(
    0, str(Path(__file__).resolve().parents[2] / "projects" / "P05-framing-crc" / "python")
)

import twin  # noqa: E402
from test_frame import c_crc16, c_decode, c_encode  # noqa: E402

CASES = 3000
MAX_PAYLOAD = 64
LANGUAGES = ("C", "C++", "Python", "Rust")

# The C's result codes, named the way the other three name them.
RC_NAME = {
    0: "ok", 1: "empty", -1: "args", -2: "stuffing",
    -3: "too_short", -4: "too_long", -5: "checksum",
}

# The one input on which the C's name differs. See the module docstring.
LONE_CODE_BYTE = b"\x01"


class ViaC:
    def crcs(self, datas):
        return [c_crc16(d) for d in datas]

    def frames(self, payloads):
        return [c_encode(p) or None for p in payloads]

    def verdicts(self, stuffeds):
        out = []
        for s in stuffeds:
            rc, payload = c_decode(s)
            out.append((RC_NAME[rc], payload if rc == 0 else None))
        return out


class ViaPython:
    def crcs(self, datas):
        return [twin.crc16(d) for d in datas]

    def frames(self, payloads):
        return [twin.encode(p) for p in payloads]

    def verdicts(self, stuffeds):
        return [twin.decode(s) for s in stuffeds]


class ViaFilter:
    """The C++ and the Rust, through the three-verb protocol both speak."""

    def __init__(self, path):
        self.path = path

    def crcs(self, datas):
        return [int(a, 16) for a in run_filter(self.path, ("C " + d.hex() for d in datas))]

    def frames(self, payloads):
        return [
            None if a == "REFUSED" else bytes.fromhex(a)
            for a in run_filter(self.path, ("E " + p.hex() for p in payloads))
        ]

    def verdicts(self, stuffeds):
        out = []
        for a in run_filter(self.path, ("D " + s.hex() for s in stuffeds)):
            name, _, payload = a.partition(" ")
            out.append((name, bytes.fromhex(payload) if payload != "-" else None))
        return out


def available():
    """Every language that can be run here. A filter that has not been built is
    left out rather than skipped as a whole test, so a laptop with no cargo
    still compares three and the table says so."""
    present = {"C": ViaC(), "Python": ViaPython()}
    if FRAME_CPP_FILTER.exists():
        present["C++"] = ViaFilter(FRAME_CPP_FILTER)
    if FRAME_RUST_FILTER.exists():
        present["Rust"] = ViaFilter(FRAME_RUST_FILTER)
    return present


def random_payload(rng, lo=1):
    return bytes(rng.randrange(256) for _ in range(rng.randrange(lo, MAX_PAYLOAD + 1)))


def payload_cases():
    """The random run plus the cases byte stuffing exists for."""
    rng = random.Random(20261003)
    cases = [random_payload(rng) for _ in range(CASES)]
    cases += [
        b"\x00", b"\x00" * 10, b"\x00\x01\x00\x02\x00", bytes(64),
        b"\x01" + bytes(62) + b"\x02",
        bytes((i % 255) + 1 for i in range(MAX_PAYLOAD)),   # crosses the 254 code boundary
        bytes.fromhex("2405FFFFE8"),                          # P09's payload, nine bytes on the wire
        b"",                                                  # the empty payload frames too
    ]
    return cases


def corruption_cases():
    """Frames corrupted three ways, delimiter stripped, plus the two edge inputs
    every decoder must name the same way."""
    rng = random.Random(20261004)
    cases = []
    for _ in range(CASES):
        frame = c_encode(random_payload(rng))
        bad = rng.choice((twin.flip_one_bit, twin.truncate, twin.inject_zero))(frame, rng)
        cases.append(bad[:-1])
    cases += [b"", LONE_CODE_BYTE, b"\x02", b"\x00", b"\xff"]
    return cases


# --------------------------------------------------------------- the tests

def test_every_language_reproduces_the_check_value():
    for name, impl in sorted(available().items()):
        assert impl.crcs([b"123456789"]) == [0x29B1], "{} does not reach 0x29B1".format(name)


def test_every_language_produces_identical_frames(capsys):
    cases = payload_cases()
    results = {name: impl.frames(cases) for name, impl in available().items()}
    reference = results["C"]

    for name, got in sorted(results.items()):
        for i, (want, have) in enumerate(zip(reference, got)):
            assert have == want, "case {}: {} framed {} as {} and C as {}".format(
                i, name, cases[i].hex(), None if have is None else have.hex(),
                None if want is None else want.hex(),
            )

    nine = reference[cases.index(bytes.fromhex("2405FFFFE8"))]
    assert len(nine) == 9, "P09's payload must frame to nine bytes"

    with capsys.disabled():
        print("\n  P05 parity over {} payloads".format(len(cases)))
        for name in LANGUAGES:
            print("    {:<7} {}".format(name, "agrees" if name in results else "not built, not compared"))


def test_every_language_gives_the_same_verdict_on_corruption():
    cases = corruption_cases()
    results = {name: impl.verdicts(cases) for name, impl in available().items()}
    reference = results["C"]

    for name, got in sorted(results.items()):
        for i, ((want_v, want_p), (have_v, have_p)) in enumerate(zip(reference, got)):
            if cases[i] == LONE_CODE_BYTE and name != "C":
                continue  # pinned separately, below
            assert have_v == want_v, "case {}: {} says {} and C says {} for {}".format(
                i, name, have_v, want_v, cases[i].hex()
            )
            assert have_p == want_p, "case {}: {} delivered {} and C {}".format(
                i, name, have_p, want_p
            )


def test_the_lone_code_byte_is_where_c_names_the_refusal_differently():
    """Every implementation refuses the single byte 0x01. The C calls it a
    stuffing error; the other three call it too short, which is what it is.
    Asserted here so that correcting the C turns this test red on purpose."""
    results = {name: impl.verdicts([LONE_CODE_BYTE])[0] for name, impl in available().items()}
    assert results["C"] == ("stuffing", None)
    for name, verdict in results.items():
        if name != "C":
            assert verdict == ("too_short", None), "{} says {}".format(name, verdict)


def test_the_comparison_was_not_vacuous():
    assert_not_vacuous(available(), "P05")


def test_rust_is_compared_when_cargo_has_been_run():
    """Named separately so the CI log says whether Rust was in the comparison."""
    if not FRAME_RUST_FILTER.exists():
        pytest.skip(
            "{} is missing. Build it in WSL on bing@JPTOUPM678 with:\n"
            "    cargo build --release --workspace".format(FRAME_RUST_FILTER.name)
        )
    answers = run_filter(FRAME_RUST_FILTER, ["C 313233343536373839"], required=True)
    assert answers == ["29B1"]
