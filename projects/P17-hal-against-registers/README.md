# P17: The same driver twice

Status: c=host cpp=host python=host rust=host

**Started on the host, Tuesday 27 October 2026.** Chapter 17's criterion 1
compares four implementations of one bus read, and that needs the peripheral and
the shield. Three of its other criteria are arithmetic on counter values and are
now written and proven: the instrument's own cost subtracted visibly, the first
run kept apart from the median of the remainder, and the timebase substitution
with each wrap period stated.

**What it is.** One operation written four ways, compared on cycles and bytes
with every condition attached.

## Which criteria this serves, and which it does not

| Criterion | State |
|---|---|
| "the instrument's own cost is measured and subtracted, and the subtraction is visible in the source rather than folded into a constant" | **the arithmetic is written and proven**, and it refuses rather than wrapping |
| "the first run and the median of the remainder are reported separately" | **written and proven**, including that nothing orders the two |
| "the timebase substitution is demonstrated on all three peripherals, with the wrap period of each stated" | **the wrap periods are computed and the chapter's three are confirmed**; the demonstration on the peripherals is bench work |
| criterion 1, four implementations byte identical | needs the shield and the bus |
| the cycle counts, the code sizes, the ratio of overhead to work | need a running target |

## What runs

| Piece | What proves it |
|---|---|
| `c/tbmath.c`, elapsed ticks, wrap periods, the subtraction, the report | 6 elapsed cases, 3 wrap periods, 2 subtractions, 4 reports, 13 refusals |
| `python/tbmath.py`, the twin, standard library only | the same cases, and both of the chapter's printed forms kept unused |
| `rust/src/lib.rs`, no_std, and the one language where the defect is noisy | the same cases through a filter, plus seven unit tests |
| `cpp/tbmath.hpp`, header only, and the project where C++ adds almost nothing | the same cases through the same protocol |
| `vectors.json`, the oracle | integers throughout, **compared exactly** |

The name follows `c/instr/freqmath.c` and its siblings: arithmetic split out of a
file that touches registers, so it can be tested on a host.

## The chapter's three wrap periods are right

A confirmation rather than a finding, and worth stating as one.

| Timebase | Width | Rate | Recomputed | Step 8 says |
|---|---|---|---|---|
| cycle counter | 32 bits | 280 MHz | **15.339 s** | about 15.3 seconds |
| general timer | 32 bits | 1 MHz | **71.583 min** | about 71.6 minutes |
| SysTick | 24 bits | 280 MHz | **59.919 ms** | about 60 milliseconds |

The test derives them from the width and the frequency rather than quoting them,
so the figures in the chapter stay checked rather than trusted.

## The timebase struct has no field for the one thing that differs

Step 8's interface carries a start function, a tick reader, a frequency and a
width in bits. **The comment beside its third instance says "counts down", and
there is no field for the direction.**

| One pair of readings of a 24-bit down counter | Elapsed |
|---|---|
| with the direction known | 100 ticks, **357.1 ns** |
| through step 8's struct, which can only subtract one way | 16777116 ticks, **59.918 ms** |

So an elapsed interval computed through the abstraction is wrong for one of the
three peripherals it exists to make interchangeable, and wrong in the direction
that looks plausible: a short interval reads as nearly a whole wrap. `tb_elapsed`
takes the direction as an argument. This is the chapter's own subject turned on
the chapter, which is that an abstraction hides the thing that matters.

## The instrument subtraction reports one whole wrap instead of refusing

Step 6 calibrates the cost of reading the counter as the minimum over 64
back-to-back reads, then returns `(b - a) - cyc_overhead` on a `uint32_t`.

| An interval of 5 ticks against a calibrated cost of 10 | Result |
|---|---|
| step 6 as printed | **4294967291 ticks, 15.339 s** |
| here | **refused**, with both inputs in the refusal |

A minimum over 64 trials is the floor of what was observed, not of what can
happen, so an interval below it is reachable. **And the bogus figure is exactly
one wrap of the cycle counter**, 15.339 s, so it is the same size as a real
duration this timebase can produce and does not look like an error at all. An
unsigned subtraction that can produce the largest number in the type is not a
measurement.

## What is not here

The four implementations, the bus, the sensor, every cycle count and code size,
the ratio of overhead to work, the pin toggle of step 7 and the Rust variant's
peripheral module routing. **There is no measured cycle in this project**: every
number it has seen was synthetic. Whether the cycle counter works on this part
with no debugger attached is on the volume's confirm list and is not assumed
either way here.

**What it needs on the bench.** A running target, which means P01's toolchain and
a flashing tool, plus the shield for the bus read.

Chapter 17 of [the book][book] is the written design.

[book]: ../../chapters/17-the-same-driver-twice.md
