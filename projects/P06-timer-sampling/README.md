# Sampling on a timer at exactly 1 kHz

Status: c=links cpp=written python=host rust=written

Chapter 6 of the NUCLEO-H7A3ZI-Q firmware volume. Three ways to sample at
1 kHz, one application, and an external witness that decides which of them
actually does.

**Four implementations of the witness since Saturday 3 October 2026**, which is
the half of this project that can be proven. The three back ends still refuse at
run time rather than guessing a setting RM0455 governs; the witness is arithmetic
over a recording, and it is the piece that decides whether a clean square wave at
a plausible wrong rate is reported as a pass.

`python/tests/test_rate_parity.py` drives nine synthetic captures through every
implementation. The one that matters is the second: a clean wave at 1100 Hz,
which every implementation must fail on the rate while passing the jitter
criterion. It is compared numerically to a relative tolerance of 1e-12 rather
than exactly, because floating-point contraction is not identical across four
toolchains, and 1e-12 is nine orders of magnitude tighter than the 0.1 percent
the measurement claims.

Two language findings came out of writing it. The Rust crate is the only one in
the workspace that is not `no_std`, because `f64::sqrt` lives in `std` and not in
`core`, while `rate.c` compiles for the target unchanged: the same program is
portable to this part in C and is not in Rust without a crate. And the Rust crate
needs version 1.77 for `round_ties_even`, because Python's `round` and C's
`nearbyint` round half to even where Rust's `round` does not.

**Started Wednesday 30 September 2026.** Nothing here has been compiled or run
on hardware. What has been run is the host-side analysis, against synthetic
input, which is deliberately the first thing rather than the last.

## What this claims, and what would refute it

The firmware claims it samples at exactly 1000.0 Hz. `MEASUREMENT.md` says
what "exactly" is allowed to mean, in four criteria that can fail, and it was
written before any hardware was connected. That order is the point: a method
written after the first plot is a rationalisation of whatever the plot showed.

The criteria in short: the mean rate within 0.1 percent, the interval spread
below 10 microseconds, no single interval more than 50 microseconds off, and
zero dropped or doubled edges.

**Why all four and not just the first.** The synthetic test demonstrated the
trap: with one edge dropped from a thousand, the mean rate was still within
1 Hz of nominal. A rate criterion alone cannot establish a timing claim.

## The three builds

The application source is byte identical across all three. Only the back end
behind `acq.h` changes, selected at build time, so the comparison is of
mechanisms rather than of three programs.

| Build | Mechanism | Prediction |
|---|---|---|
| `sampling-systick` | The tick exception converts, waits, stores | Worst on spread and worst case |
| `sampling-timer` | A timer's update event starts the converter in hardware | Better, and the processor is out of the timing path |
| `sampling-dma` | As above, and a transfer engine moves the samples | Indistinguishable from `timer` on spread |

**The prediction is written down so that finding otherwise is a result rather
than a surprise to be explained away.**

One honest weakness, stated in `acq_timer.c` rather than buried: the marker
is still toggled in software, so the marker edge carries the interrupt latency
even where the sample instant does not. The witness therefore measures the
regularity of the handler. Letting a timer channel drive the pin directly is the
only way this method can separate the two, and it is the stretch goal.

## The witness

An **MCC 118 DAQ HAT on a Raspberry Pi 4**: 12-bit, 100 kS/s aggregate. On one
channel that is about 100 samples per period of a 1 kHz square wave, so an edge
time resolves to roughly 10 microseconds.

**That resolution is the limit on criterion 3**, which is therefore a coarse
check on gross faults rather than a precision figure. Criterion 2 survives
because the spread of many intervals is better resolved than any single one.
The analysis says so in its own output rather than leaving the reader to work
it out.

There is no oscilloscope and no logic analyser on this bench, which is why the
witness is a data acquisition HAT rather than either.

## Layout

    acq.h              one interface, three implementations
    acq_systick.c      build 1
    acq_timer.c        build 2
    acq_dma_double.c   build 3, and the two traps it sits on
    marker.c           the one pin the witness watches
    main.c             byte identical across all three builds
    ../../c/instr/cyccnt.c         cycle counter, with a self-test rather than a hope
    ../../c/instr/freqcount.c      gated counter, for checking the witness
    scan.py        capture, on the Pi
    ../../python/firmkit/rate.py        the analysis, two independent routes that must agree
    ../../python/tests/test_rate.py   the analysis tested on synthetic input
    MEASUREMENT.md    the method, written before the first run

## What runs today, with no hardware

    cd witness
    python test_rate.py

Four cases whose answers are known by construction. Two of them prove the
analysis **refuses** bad input, which is the half that matters: an analysis that
passed everything would have passed the board too.

## What is deliberately not finished

Every back end returns a negative value from `acq_start` rather than running
with a guessed peripheral setting, and `main.c` prints which and stops.

That is not an oversight. **This part is documented by RM0455, not the RM0433
that most material saying "STM32H7" was written against**, and the clock tree,
the power configuration and the memory map all differ. A build that guessed the
timer clock would produce a clean square wave at the wrong rate, and the whole
point of this chapter is that such a wave is indistinguishable from a right one
without an external witness.

Every place a value is needed carries a `TO BE CONFIRMED` comment naming what to
read. A board that refuses to run is better than one that runs and lies.

## Vendor code

The device header and the startup file come from the vendor pack and are **not
vendored into this repository**, because that pack's licence differs file by
file and a project that pasted it in would be redistributing terms it had not
read. Point CMake at wherever it was unpacked:

    cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
      -DCMSIS_DEVICE_DIR=<path> -DSTARTUP_FILE=<path>

No generated code appears here as though it had been reviewed.
