# The measurement method, written before the first run

Written on Wednesday 30 September 2026, before any hardware was connected.
That order is the point. A method written after the first plot is a
rationalisation of whatever the plot happened to show.

## What is being claimed

The firmware claims it samples at **exactly 1000.0 Hz**. This document says what
"exactly" is allowed to mean, what would refute the claim, and how the number is
arrived at, before any number exists.

## The claim in a form that can fail

The firmware toggles one pin once per sample. The witness records that pin and
recovers the interval between edges. The claim passes only if all four hold:

| # | Quantity | Passes if | Refuted if |
|---|---|---|---|
| 1 | Mean rate over a 60 second run | within 0.1 percent of 1000.0 Hz | outside that band |
| 2 | Interval spread, standard deviation | below 10 microseconds | above it |
| 3 | Worst single interval in the run | within 50 microseconds of nominal | any interval outside |
| 4 | Dropped or doubled edges | zero | one or more |

Criterion 1 without 2 and 3 is the trap this whole exercise exists to avoid: a
counter can average correctly while every individual interval is wrong, and a
timer that misses one tick in ten thousand still averages to within a whisker of
right.

## The three builds under test

The application source is byte identical across all three. Only the acquisition
back end changes, selected at build time, so the comparison is of the mechanism
and not of the program.

1. **System tick.** The tick exception fires at 1 kHz, converts, stores, returns.
   The simplest thing that works, and a real variant rather than a strawman.
2. **Timer trigger.** A timer's update event starts the converter in hardware.
   The processor is not in the path of the timing at all.
3. **Timer trigger with a double-buffered transfer.** As above, and the samples
   reach memory without the processor touching them.

The expected ordering is that 1 is worst on criteria 2 and 3 and that 2 and 3
are indistinguishable on them, because in both the edge is produced by hardware.
**That expectation is written here so that finding otherwise is a result rather
than a surprise to be explained away.**

## The instrument

The witness is an **MCC 118 data acquisition HAT on a Raspberry Pi 4**: eight
single-ended analogue inputs, 12-bit, 100 kS/s aggregate. Sampling one channel,
the full aggregate rate is available, so a 1 kHz square wave is sampled about
100 times per period. That is ample for an edge time to about one sample period,
which is 10 microseconds.

**This is the limit that decides criterion 3.** An edge time resolved to 10
microseconds cannot support a claim about a 50 microsecond outlier with much
margin, so criterion 3 is a coarse check on gross faults and not a precision
figure. Criterion 2 survives because the standard deviation of many intervals is
better resolved than any single one.

Two things about the instrument are **not yet read from its user guide** and are
open questions, not assumptions:

- The input range, and therefore whether a 3.3 V logic swing sits comfortably
  inside it. Every range this HAT is likely to offer contains 3.3 V, which is a
  reason to expect it to be fine and not a reason to skip reading.
- Whether it offers an external trigger, which would allow the board to start
  the capture rather than the two running independently.

There is **no oscilloscope and no logic analyser on this bench**, which is why
the witness is a data acquisition HAT rather than either of those. Joseph's own
skill with both is at basics to intermediate, and neither instrument is present,
so nothing here depends on one.

## What the witness does not measure

The witness sees the pin, not the conversion. The interval between edges is the
interval between the moments the firmware decided to toggle. If the conversion
itself is late, or if the stored sample belongs to a different instant than the
edge suggests, this method cannot see it. Establishing that would need the
converter's own timing, which is a different measurement and is not claimed here.

## Procedure

1. Ground wire first, before any signal wire.
2. Record the ambient conditions and the build identity, from `git describe`,
   into the metadata file beside the samples. A capture with no build identity
   is not evidence.
3. Run 60 seconds per build, three runs per build, nine runs in total.
4. Analyse every run with the same script and no manual steps.
5. Report median, spread and worst case per build. Report all nine runs, not the
   best one.

## How the rate is computed

Two independent routes, and they must agree. If they disagree the analysis is
wrong, not the firmware.

- **Counting.** Number of rising edges divided by the time between the first and
  last. Robust, and insensitive to any single edge being mistimed.
- **Fitting.** A straight line fitted to edge index against edge time. Its
  gradient is the period. This uses every edge and yields an uncertainty.

The reported rate is the fit. The count is the check on the fit.

## Uncertainty

The uncertainty on the fitted rate comes from the fit, not from a guess. It is
reported and it is not optional: a rate quoted without one is not a measurement.
The dominant term is expected to be the witness's own sample clock, whose
accuracy is a further open question against its user guide.

## Before the analysis is trusted on real data

`witness/test_rate.py` runs the analysis against synthetic input whose answer is
known by construction: a clean train, a train with jitter, a train with a
dropped edge, and a train at a deliberately wrong rate. The analysis must
recover the right answer from the first two and refuse the last two. That test
runs on any machine with Python and needs no hardware, which is why it was
written and run first.
