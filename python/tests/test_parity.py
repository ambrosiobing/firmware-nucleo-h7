"""Four languages, one oracle, byte for byte.

Since Saturday 3 October 2026 every project in this volume carries C, C++,
Python and Rust, and the claim is not that four implementations exist but that
they are *equivalent*. This is the test that makes that claim falsifiable for
P09, and it is the shape every other project's parity test will copy.

**The oracle belongs to the project, not to a language.** For P09 it is
`projects/P09-payload-codec/vectors.json`: six vectors, hand computed, which no
generator has ever touched and which none of the four implementations can see.
Three of the six exist to catch a specific defect that positive test values hide:
the second catches reading 131071 as eighteen set bits, the third and fourth
catch a missing sign extension, and the fifth catches an off-by-one in the field
width.

**Why this is not four tests.** Four separate suites, one per language, would each
pass while the four disagreed with each other, because each would compare an
implementation against its own idea of the layout. Here the comparison is
pairwise against one reference and over one shared case list, so a disagreement
is reported as a disagreement rather than as four passes.

**C is the reference.** Not because it is first but because it is the one that has
run on the board: P09's C image printed 2405FFFFE8 on the Cortex-M7 on Saturday
3 October 2026, which is the only figure here that came from hardware. Every
other implementation is compared against the one whose output was observed on
the part.

**What a skip means.** A language whose implementation is not built is left out
of the comparison, and the printed table says which. `assert_not_vacuous` in
conftest.py then refuses a run that compared fewer than three: a parity suite
that passes because everything was absent is the failure mode this test exists
to prevent, and it is the one failure mode such a suite cannot notice about
itself. On Windows that refusal is a skip naming the WSL command, because two of
the four reach the comparison through a compiler and compiling on win11
aquamarine is forbidden. In WSL and in CI the bar is the full one.
"""
from __future__ import annotations

import os
import random

import pytest
from firmkit import payload

from conftest import (
    CPP_FILTER,
    RUST_FILTER,
    assert_not_vacuous,
    c_decode,
    c_encode,
    run_codec_filter,
)
from test_roundtrip import SEED, random_case

CASES = int(os.environ.get("CODEC_CASES", "100000"))

# The four, in the order they were written. The value is how to get the pair
# (uppercase hex, decoded fields) out of each for a list of cases.
LANGUAGES = ("C", "C++", "Python", "Rust")


def through_c(lib, cases):
    """Encode once per case and decode that, rather than encoding twice. With a
    hundred thousand cases the second call would double the ctypes crossings for
    nothing."""
    out = []
    for case in cases:
        raw = c_encode(lib, case)
        out.append((raw.hex().upper(), c_decode(lib, raw)))
    return out


def through_python(cases):
    out = []
    for case in cases:
        raw = payload.encode(case)
        out.append((raw.hex().upper(), payload.decode(raw)))
    return out


def available(lib, cases):
    """Every language that can actually be run here, with its results.

    Returns a dict rather than a list so a failure message can name the two
    languages that disagreed. The two filters are skipped individually: a laptop
    with no cargo still compares three, and says so.
    """
    results = {"C": through_c(lib, cases), "Python": through_python(cases)}
    for name, path in (("C++", CPP_FILTER), ("Rust", RUST_FILTER)):
        if path.exists():
            results[name] = run_codec_filter(path, cases)
    return results


def test_every_language_matches_the_vectors(lib, vectors, capsys):
    """The six hand-computed vectors, against every language present."""
    cases = [v["fields"] for v in vectors]
    results = available(lib, cases)

    for name, got in sorted(results.items()):
        for vector, (hex_bytes, decoded) in zip(vectors, got):
            assert hex_bytes == vector["bytes"], (
                "{}: {} encoded {} and the vector says {}".format(
                    vector["name"], name, hex_bytes, vector["bytes"]
                )
            )
            assert decoded == vector["fields"], (
                "{}: {} decoded {} and the vector says {}".format(
                    vector["name"], name, decoded, vector["fields"]
                )
            )

    with capsys.disabled():
        print("\n  parity over {} hand-computed vectors".format(len(vectors)))
        for name in LANGUAGES:
            state = "agrees" if name in results else "not built, not compared"
            print("    {:<7} {}".format(name, state))


def test_every_language_agrees_with_c_over_the_random_run(lib):
    """The same recorded-seed run through every language present.

    The seed is recorded rather than random so a disagreement can be reproduced
    exactly, and `code.yml` runs a separate job with a fresh seed so a layout
    defect that this seed happens to miss is still found eventually.
    """
    rng = random.Random(SEED)
    cases = [random_case(rng) for _ in range(CASES)]
    results = available(lib, cases)
    reference = results["C"]

    for name, got in sorted(results.items()):
        if name == "C":
            continue
        for i, ((want_hex, _), (hex_bytes, decoded)) in enumerate(zip(reference, got)):
            if hex_bytes != want_hex:
                pytest.fail(
                    "case {} of {}: {} encoded {} and C encoded {} for {}".format(
                        i, len(cases), name, hex_bytes, want_hex, cases[i]
                    )
                )
            if decoded != cases[i]:
                pytest.fail(
                    "case {} of {}: {} decoded {} and the input was {}".format(
                        i, len(cases), name, decoded, cases[i]
                    )
                )


def test_the_comparison_was_not_vacuous(lib, vectors):
    """At least three of the four must have actually run.

    Without this, every assertion above passes on a machine where nothing is
    built, because there is nothing to disagree. Three rather than four because
    Rust needs a toolchain this volume does not require of a reader, and C, C++
    and Python all come from `python/tools/build_host.py`, which the suite's own
    instructions already require. The guard is shared with P05's parity test, in
    conftest.py, where the Windows case is explained.
    """
    assert_not_vacuous(available(lib, [v["fields"] for v in vectors]), "P09")


def test_rust_is_compared_when_cargo_has_been_run(lib, vectors):
    """Named separately so the CI log says whether Rust was in the comparison.

    This is the one that turns red when the Rust job is removed from `code.yml`
    but the status key still says the language is proven on the host.
    """
    if not RUST_FILTER.exists():
        pytest.skip(
            "{} is missing. Build it in WSL on bing@JPTOUPM678 with:\n"
            "    cargo build --release --workspace".format(RUST_FILTER.name)
        )
    got = run_codec_filter(RUST_FILTER, [v["fields"] for v in vectors], required=True)
    for vector, (hex_bytes, decoded) in zip(vectors, got):
        assert hex_bytes == vector["bytes"], vector["name"]
        assert decoded == vector["fields"], vector["name"]
