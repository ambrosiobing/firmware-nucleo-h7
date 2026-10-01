#!/usr/bin/env python3
"""Prove the capacity assertion stops the build, by breaking it on purpose.

    python python/tools/check_ring_assert.py

P02's acceptance criteria say the capacity assertion must stop the build when the
capacity is changed to something that is not a power of two, "verified by
changing it on purpose once". Once is the problem: a check performed by hand on
one afternoon and then trusted is a check nobody is running. This does it on
every test run and costs a second.

Three bad capacities are tried and each must fail to compile. Then a good one is
tried and must succeed, because a test that only ever expects failure would pass
just as happily against a compiler that refused everything.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent   # python/tools -> repo root
sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_host import WARNINGS, compiler_env, find  # noqa: E402

MUST_FAIL = (
    (100, "not a power of two"),
    (255, "one less than a power of two, the classic off-by-one capacity"),
    (1, "a capacity of one leaves no room to be full"),
)
MUST_PASS = (2, 256, 4096)


def compile_with(cc: str, capacity: int, out: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            cc, "-std=c11", "-O0", *WARNINGS,
            "-DRING_SIZE={}u".format(capacity),
            "-I", str(ROOT / "c" / "ring"),
            "-c", str(ROOT / "c" / "ring" / "ring.c"),
            "-o", str(out),
        ],
        capture_output=True,
        text=True,
        env=compiler_env(cc),
    )


def main() -> int:
    cc = find("gcc")
    failures = []

    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "ring.o"

        for capacity, why in MUST_FAIL:
            proc = compile_with(cc, capacity, out)
            if proc.returncode == 0:
                failures.append(
                    "RING_SIZE={} compiled, and it must not: {}".format(capacity, why)
                )
                continue
            if "_Static_assert" not in proc.stderr and "static assertion" not in proc.stderr:
                failures.append(
                    "RING_SIZE={} failed, but not on the assertion:\n{}".format(
                        capacity, proc.stderr.strip()[:400]
                    )
                )
                continue
            print("  refused  RING_SIZE={:<6} {}".format(capacity, why))

        for capacity in MUST_PASS:
            proc = compile_with(cc, capacity, out)
            if proc.returncode != 0:
                failures.append(
                    "RING_SIZE={} must compile and did not:\n{}".format(
                        capacity, proc.stderr.strip()[:400]
                    )
                )
                continue
            print("  accepted RING_SIZE={:<6} a power of two, at least two".format(capacity))

    if failures:
        print("\nthe capacity assertion is not doing its job:")
        for f in failures:
            print("  " + f)
        return 1
    print("the capacity assertion refuses every bad value and accepts every good one.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
