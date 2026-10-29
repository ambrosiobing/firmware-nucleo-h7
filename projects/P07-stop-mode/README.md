# P07: Stop mode, RTC wake, and a battery number

Status: c=host cpp=none python=host rust=host

**Started on the host, Monday 26 October 2026, and chapter 7 asked for it in
these words.** Step 9: "Test the analysis on a synthetic trace. The segmentation
is the fragile part, so it is tested on the host with a trace whose answer is
known by construction, in continuous integration, with no instrument attached."
So the segmentation of step 8, the report step 8 defines, and the battery
arithmetic of step 10 are written, and all three turned out to have something
wrong with them, including a shipped test whose expected value its own code does
not produce.

**What it is.** Charge per wake cycle with a spread rather than an average
current, and the trap that current after wake stays high because the clock tree
is not restored.

## What runs

| Piece | What proves it |
|---|---|
| `c/cyclecharge.c`, the segmentation, the report, the battery sum | 5 traces, 5 reports, 4 lifetimes and 14 refusals |
| `python/cyclecharge.py`, the twin, standard library only | the same cases, and both of the chapter's printed functions kept unused |
| `rust/src/lib.rs`, `no_std` with `f64`, and `rust/src/main.rs`, the filter | the same cases through a three verb protocol, every float exact, and six unit tests |
| `vectors.json`, the oracle | **every case exact in binary**: currents are powers of two and the sample rate is 1024 Hz |

**How this differs from P10**, which also integrates a current trace. P10 decodes
a marker *code* carried on every sample and attributes charge to a phase. This
segments on marker *edges*, cycle to cycle, and reports the spread across cycles,
which is the thing the chapter says decides a battery life: "an average current
over a duty-cycled trace hides the thing that decides a battery life, which is
whether the cycles are alike".

**The traces are a recipe rather than a list of numbers.** The oracle stores the
period, the marker width, the pulse count and the current per cycle, and the test
builds the trace from that in ten lines. A recipe is checkable by a reader; three
hundred numbers are not.

## Step 9's own test asserts a count its own code does not produce

Its trace tiles **ten** marker pulses and begins while the marker is already
high, so the first cycle has no rising edge in front of it. Ten pulses bound
**eight** complete cycles.

| | Cycles |
|---|---|
| marker pulses in step 9's trace | 10 |
| what step 9 asserts, commented "nine complete cycles in ten" | 9 |
| what step 8's printed code returns | **8** |
| what this implementation returns | **8** |

The printed code and this one agree, which is what says eight is right. The
defect is in the assertion, and a test whose expected value is wrong is the one
thing a reader checks the code against.

## The printed window takes one sample from the previous cycle

`np.diff` puts a rising edge at the index *before* the rise, and the sum then
runs from that index, so every cycle carries one sample belonging to the cycle
before it. One sample of 4096 microamps at index 23, the last low sample before a
rise and therefore still inside the first cycle, is attributed to the **second**
cycle by the chapter's window and to the **first** here, a difference of 4
microcoulombs in a 2 microcoulomb cycle.

**On the flat trace step 9 uses, the two windows agree exactly**, because one
sample of 100 microamps replaces another. That is why the shipped test passes on
the charge, and it is the clearest case in this repository of a test that cannot
see the defect in the code it covers.

## Step 10's lifetime leaves most of the day free

| Cycles per day | Seconds covered of 86400 | Step 10 as printed | Costing the rest at 2 µA | Factor |
|---|---|---|---|---|
| 86400 | 86400 | 85.2 days | 85.2 days | 1.0 |
| 1440 | 1440 | 5113.6 days | 2345.7 days | 2.2 |
| 100 | 100 | **73636.4 days** | **4032.7 days** | **18.3** |

The arithmetic takes cycles per day as a free assumption while the trace already
fixes the cycle period, and nothing checks that the product is a day. Where it is
not, the uncovered seconds cost nothing at all in that sum. The chapter's list of
what the arithmetic does not model names self-discharge, the capacity a coin cell
delivers under pulsed load, temperature, the regulator's efficiency and the fact
that the board is not the product. It does not name this.

**With no sleep current assumed the two forms agree**, which is the control: the
finding is about a missing assumption and not about the arithmetic.
`cyc_lifetime` reports the covered and uncovered seconds, costs the remainder at
a stated sleep current, and prints the chapter's form beside its own.

**The report also refuses fewer than ten cycles**, because step 8's instruction
to report the median and the extremes over at least ten is a rule: a median and a
spread over three cycles is a number that looks like the one it asks for and is
not.

## What is not here

The instrument and its GPL-2.0 interface, the resistor check that gives it a
working uncertainty, the run-state inventory, the stop-mode entry sequence from
RM0455's low-power section, the clock restore and the two traces that show what
it costs, the low-power timer variant, the tickless variant and the three marker
pins. **There is no measured number anywhere in this project**: every trace it
has seen was synthetic, which is exactly what step 9 asks of it, and the status
line says host for that reason.

**What it needs on the bench.** Nucleo plus the nRF PPK2.

Chapter 7 of [the book][book] is the written design.

[book]: ../../chapters/07-stop-mode.md
