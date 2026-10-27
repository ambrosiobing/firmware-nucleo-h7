# P17 in C

**State: host, since Tuesday 27 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI.

| File | What it is |
|---|---|
| `tbmath.h` | which criteria this serves, the two findings, the seven refusals |
| `tbmath.c` | elapsed ticks, the wrap period, the subtraction, the report |

**Why the name is tbmath.** It follows `c/instr/freqmath.c`, `pwmmath.c` and
`adcmath.c`: the arithmetic split out of a file that touches registers, so it can
be called on a host where the register file cannot. It is deliberately not
`timebase.h`, after a header called `features.h` shadowed glibc's own on Tuesday
27 October 2026 and failed the build three files away from the one at fault.

**Why the direction is an argument.** Step 8's struct has no field for it and its
own comment says one of the three counters counts down. A subtraction that
assumes the wrong direction turns a hundred ticks into nearly a whole wrap, and
on a 24-bit counter at the core clock that is 59.918 ms reported for 357 ns.

**Why a reading outside the stated width is refused.** A value that does not fit
the width is a reading from a different counter, or a width that is wrong. Either
way the elapsed interval computed from it is arithmetic on a mistake, and masking
it quietly would hide which.

**Why the subtraction refuses.** An unsigned subtraction that can produce the
largest number in the type is not a measurement. The refusal carries both inputs
so the calibration can be looked at, which is where the fault actually is.

## What it is proven against

6 elapsed cases, 3 wrap periods, 2 subtractions, 4 reports and 13 refusals,
driven through this implementation and its Python twin by
`python/tests/test_tbmath_parity.py`, every one compared exactly. The C half
skips on a laptop with no compiler, naming the WSL command, and runs in full in
CI.

## What is not here

The four implementations of the bus read, the cycle counts, the code sizes and
the pin toggle. Those need a running target and the shield.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
