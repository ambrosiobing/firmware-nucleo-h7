"""The ring buffer, all four ordering modes, through a million bytes and more.

Three properties, each catching a different class of fault:

  - the byte sequence on the far side catches a masking or wrap fault
  - the occupancy catches an index fault
  - offered equals accepted plus dropped, which catches a policy fault

The third is in the book's acceptance criteria and was not actually asserted by
the listing there, which accumulated a drop count and only printed it. It is
asserted here, twice: against the caller's own tally and against the structure's
internal counter, which are different numbers that must agree.

What this cannot do is find an ordering fault. The host is strongly ordered and
will happily run a build with every barrier removed, for a week, without
complaint. That is why RING_BARRIER=0 is expected to pass here and is still not
shippable. The ordering argument comes from the architecture reference and the
published correctness proof, and the only machine that can falsify it is the
target.
"""
from __future__ import annotations

import ctypes
import os
import random
import re
import subprocess

import pytest
from conftest import BUILD, ROOT, shared_library_name

BARRIERS = (0, 1, 2, 3)
CAPACITY = 256
STEPS = int(os.environ.get("RING_STEPS", "200000"))


class Ring(ctypes.Structure):
    """Must match ring_t in shared/ring.h, field for field and in order."""

    _fields_ = [
        ("buf", ctypes.c_uint8 * CAPACITY),
        ("head", ctypes.c_uint32),
        ("tail", ctypes.c_uint32),
        ("drops", ctypes.c_uint32),
    ]


_LIBS: dict[int, ctypes.CDLL] = {}


def lib(mode: int) -> ctypes.CDLL:
    if mode not in _LIBS:
        path = BUILD / shared_library_name("ring{}".format(mode))
        if not path.exists():
            pytest.fail(
                "{} is missing. Build it first:\n"
                "    python tools/build_host.py".format(path.name)
            )
        dll = ctypes.CDLL(str(path))
        dll.ring_init.restype = None
        dll.ring_init.argtypes = [ctypes.POINTER(Ring)]
        dll.ring_put.restype = ctypes.c_int
        dll.ring_put.argtypes = [ctypes.POINTER(Ring), ctypes.c_uint8]
        dll.ring_get.restype = ctypes.c_int
        dll.ring_get.argtypes = [ctypes.POINTER(Ring), ctypes.POINTER(ctypes.c_uint8)]
        dll.ring_used.restype = ctypes.c_uint32
        dll.ring_used.argtypes = [ctypes.POINTER(Ring)]
        dll.ring_drops.restype = ctypes.c_uint32
        dll.ring_drops.argtypes = [ctypes.POINTER(Ring)]
        dll.ring_barrier_name.restype = ctypes.c_char_p
        _LIBS[mode] = dll
    return _LIBS[mode]


@pytest.mark.parametrize("mode", BARRIERS)
def test_property_over_random_bursts(mode):
    """The heavy property test, run as a C subprocess, once per ordering mode.

    This loop is C rather than Python because the first version drove the
    structure through ctypes and took 127 seconds across the four modes, against
    the book's criterion of under two seconds. A test that takes two minutes is a
    test somebody switches off. The three properties and the reconciliation the
    book's listing omitted are all asserted inside property_test.c, which exits
    non-zero on any of them.
    """
    exe = BUILD / ("property_test{}{}".format(mode, ".exe" if os.name == "nt" else ""))
    if not exe.exists():
        pytest.fail(
            "{} is missing. Build it first: python tools/build_host.py".format(exe.name)
        )
    proc = subprocess.run(
        [str(exe), str(STEPS)], capture_output=True, text=True, timeout=300
    )
    print(proc.stdout.strip())
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "PASS" in proc.stdout
    for fault in ("ORDER FAULT", "OCCUPANCY FAULT", "POLICY FAULT"):
        assert fault not in proc.stdout

    # A real workout, not a token one. Proportional to the step count so a
    # shortened development run is still checked.
    accepted = int(re.search(r"accepted (\d+)", proc.stdout).group(1))
    assert accepted >= STEPS * 50, (
        "only {} bytes through {} steps; weaker than it claims".format(accepted, STEPS)
    )


@pytest.mark.parametrize("mode", BARRIERS)
def test_empty_and_full_boundaries(mode):
    """Full means the capacity, not the capacity less one.

    No slot is sacrificed to distinguish full from empty, so the capacity is the
    number in the header. A structure that silently held 255 bytes would pass the
    property test above and fail here.
    """
    dll = lib(mode)
    r = Ring()
    dll.ring_init(ctypes.byref(r))
    byte = ctypes.c_uint8()

    assert dll.ring_get(ctypes.byref(r), ctypes.byref(byte)) == 0, "a fresh ring is empty"
    assert dll.ring_used(ctypes.byref(r)) == 0

    for i in range(CAPACITY):
        assert dll.ring_put(ctypes.byref(r), i & 0xFF) == 1, (
            "refused byte {} of a capacity of {}".format(i, CAPACITY)
        )
    assert dll.ring_used(ctypes.byref(r)) == CAPACITY
    assert dll.ring_put(ctypes.byref(r), 0) == 0, "accepted a byte past the capacity"
    assert dll.ring_drops(ctypes.byref(r)) == 1

    for i in range(CAPACITY):
        assert dll.ring_get(ctypes.byref(r), ctypes.byref(byte)) == 1
        assert byte.value == (i & 0xFF)
    assert dll.ring_get(ctypes.byref(r), ctypes.byref(byte)) == 0, "a drained ring is empty"


@pytest.mark.parametrize("mode", BARRIERS)
def test_counters_stay_correct_across_their_own_wrap(mode):
    """head and tail are free running and must survive wrapping past 2^32.

    Reaching the wrap honestly would need four billion puts. Instead the indices
    are placed just below the boundary and the structure is driven across it,
    which exercises exactly the arithmetic the invariant relies on.
    """
    dll = lib(mode)
    r = Ring()
    dll.ring_init(ctypes.byref(r))
    byte = ctypes.c_uint8()

    near = 0xFFFFFFF0
    r.head = near
    r.tail = near
    for i in range(64):
        assert dll.ring_put(ctypes.byref(r), i & 0xFF) == 1
        assert dll.ring_get(ctypes.byref(r), ctypes.byref(byte)) == 1
        assert byte.value == (i & 0xFF)
        assert dll.ring_used(ctypes.byref(r)) == 0
    assert r.head == ctypes.c_uint32(near + 64).value
    assert r.head < near, "the counter did not actually wrap, so nothing was tested"


def test_the_two_thread_soak_runs_and_says_what_it_cannot_prove():
    """The C soak, run as a subprocess, with its own limitation in its output.

    Two real threads rather than Python ones, because Python's interpreter lock
    would serialise the two sides and the soak would exercise nothing. It is
    still not an ordering test, and it prints so.
    """
    exe = BUILD / ("soak_threads.exe" if os.name == "nt" else "soak_threads")
    if not exe.exists():
        pytest.skip("{} is missing. Run: python tools/build_host.py".format(exe.name))
    proc = subprocess.run([str(exe)], capture_output=True, text=True, timeout=180)
    print(proc.stdout.strip())
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "cannot detect an ordering fault" in proc.stdout, (
        "the soak must state its own limitation in its output"
    )


def test_the_capacity_assertion_actually_fires():
    """Verified by building it wrong on purpose, not by having checked once."""
    script = ROOT / "python" / "tools" / "check_ring_assert.py"
    proc = subprocess.run(
        ["python", str(script)], capture_output=True, text=True, cwd=ROOT, timeout=180
    )
    print(proc.stdout.strip())
    assert proc.returncode == 0, proc.stdout + proc.stderr
