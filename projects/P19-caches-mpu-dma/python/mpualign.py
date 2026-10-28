"""P19's alignment arithmetic in Python, the twin of c/mpualign.c.

Chapter 19's fourth and fifth acceptance criteria are arithmetic: every buffer
aligned to 32 bytes and sized to a multiple of 32, enforced at build time, and
a misaligned region table entry stopping the build rather than the board. This
is one side of that, and **it is the first twin this project has had**: until
Wednesday 28 October 2026 P19 held one implementation in one language and
therefore had no equivalence gate at all.

Standard library only, and no fixed width anywhere, which is the point of
comparing it. **The C computes on 32-bit addresses and this does not**, so
finding four, the end address that wraps past the top of the address space, is
a case where the two could disagree rather than a case the C alone can see.
Python is asked to refuse it by the same rule rather than by running out of
bits.

It also carries, at the bottom, the two readings the chapter's wording invites
and this project refuses. Nothing above calls them.
"""
from __future__ import annotations

MPU_LINE = 32

MPU_MIN_REGION = 32
MPU_MAX_REGION = 0x80000000

TOP = 0x100000000

ERR_ARGUMENT = -1
ERR_LINE = -2
ERR_LENGTH = -3
ERR_WRAP = -4
ERR_SIZE = -5
ERR_ALIGN = -6

ERRORS = {"argument": ERR_ARGUMENT, "line": ERR_LINE, "length": ERR_LENGTH,
          "wrap": ERR_WRAP, "size": ERR_SIZE, "align": ERR_ALIGN}


def is_power_of_two(v):
    return v != 0 and (v & (v - 1)) == 0


def maint_range(addr, length, line):
    """The whole lines a maintenance operation touches. (0, dict) or (rc, None).

    The two shared lines are reported separately, because cleaning one is
    harmless and invalidating one discards a neighbour's unwritten data.
    """
    if not is_power_of_two(line):
        return ERR_LINE, None
    if length == 0:
        return ERR_LENGTH, None

    last = addr + length
    mask = line - 1
    aligned_end = (last + mask) & ~mask
    # At or above the top, not above it. `end` is one past the last address, so
    # a range ending exactly at the top has no 32-bit value: the C would have
    # to store zero, and zero makes a loop from base to end run no times at
    # all. Refused here by the same rule, not because this language ran out of
    # bits, which it never does.
    if aligned_end >= TOP:
        return ERR_WRAP, None

    base = addr & ~mask
    return 0, {
        "base": base,
        "end": aligned_end,
        "bytes": aligned_end - base,
        "lines": (aligned_end - base) // line,
        "head_shared": 1 if (addr & mask) != 0 else 0,
        "tail_shared": 1 if (last & mask) != 0 else 0,
    }


def buffer_check(addr, length, line):
    """Criterion 4 as one answer: aligned to a line AND a whole number of them."""
    rc, r = maint_range(addr, length, line)
    if rc != 0:
        return rc
    if r["head_shared"] or r["tail_shared"]:
        return ERR_ALIGN
    return 0


def region_encode(base, size):
    """Criterion 5: the ARMv7-M SIZE field, log2(size) - 1, and the base checked
    against the region's own size. Returns (0, field) or (rc, None)."""
    # The C carries no upper test, because every power of two a uint32_t holds
    # is at or below 2 to the 31st, so such a guard could never fire there.
    # This language has no width at all, so the test is written here and
    # refuses a size the C could not have been handed. That keeps the two
    # agreeing on every case the oracle can STATE rather than only on the ones
    # the C can reach, which is the difference between a gate and a tautology.
    if not is_power_of_two(size) or size < MPU_MIN_REGION \
            or size > MPU_MAX_REGION:
        return ERR_SIZE, None
    # Finding three: the base is a multiple of the region's OWN size, not of
    # the cache line, so criterion 4 and criterion 5 are independent.
    if (base & (size - 1)) != 0:
        return ERR_ALIGN, None
    return 0, size.bit_length() - 2


# ---------------------------------------------------------------------------
# The two readings chapter 19 invites and this project refuses. Nothing above
# calls either.

def chapter_align_only(addr, line):
    """Criterion 4 read as alignment alone, which is half of what it says.

    "Every buffer is aligned to 32 bytes and sized to a multiple of 32 bytes"
    is two facts, and a reader who checks the first has removed the shared HEAD
    line and left the shared TAIL. Invalidating that tail discards whatever
    follows the buffer. A test runs this beside buffer_check on a buffer that
    passes this and fails that.
    """
    return (addr % line) == 0


def chapter_line_aligned_region(base, line):
    """Criterion 5 read as alignment to the cache line, which is finding three.

    An ARMv7-M region must start on a multiple of its own size. A reader who
    has internalised "align to 32 and you are safe" checks this instead, and a
    4096 byte region at 0x24000100 passes it while the hardware refuses the
    entry.
    """
    return (base % line) == 0
