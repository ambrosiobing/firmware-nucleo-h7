# P16 in C

**State: host, since Tuesday 27 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI, which compiles the same
source with the whole warning set.

| File | What it is |
|---|---|
| `winfeat.h` | the window definition, the three findings at length, the six refusals |
| `winfeat.c` | the three pinned feature groups and the bounded tree walk |

**Why the integer square root is written out.** A floating-point `sqrt` followed
by a cast would agree with the Python almost always, and almost always is what an
exact gate exists to refuse. This is a bit-by-bit descent, so the result is the
same on every implementation and on a part with no floating-point unit, and the
Python uses `math.isqrt`, which is a different algorithm for the same floored
quantity. Two algorithms agreeing on every case is the gate doing work.

**Why the floor division is spelled out rather than shifted.** A right shift of a
negative signed value is implementation defined in C, and the direction of this
rounding is the thing chapter 16 left open and this project pins. It belongs in
the source, not in a compiler's choice.

**Why a window of the wrong shape is refused rather than scaled.** Step 3 fixes
128 samples of three axes. A feature computed over a different length is a
different feature, and a gate comparing one against the other would be comparing
two definitions.

## What it is proven against

6 feature cases, 4 shape refusals, 5 tree cases and 6 tree refusals, driven
through this implementation and its Python twin by
`python/tests/test_winfeat_parity.py`, every one compared exactly. The C half
skips on a laptop with no compiler, naming the WSL command, and runs in full in
CI.

## What is not here

The band energies, for the reason the header gives: the transform they come from
is unspecified and cannot enter an exact gate. The three legs, the in-sensor
program and every measured number need the shield and a Raspberry Pi.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
