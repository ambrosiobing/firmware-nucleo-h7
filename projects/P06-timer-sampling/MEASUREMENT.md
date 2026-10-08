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

## Status of each criterion, Wednesday 7 October 2026

**Nothing above has been changed since it was written, and nothing above has yet
been evaluated.** This section is appended rather than edited, because the value
of this file is that it was fixed before the first run.

| Criterion | State |
|---|---|
| 1. Mean rate within 0.1 percent | **not evaluated** |
| 2. Interval spread below 10 microseconds | **not evaluated** |
| 3. No single interval more than 50 microseconds off | **not evaluated** |
| 4. Zero dropped or doubled edges | **not evaluated** |

The firmware began sampling on the board that day, 24 960 conversions on a
hardware trigger with no lost blocks, and that bears on **none** of the four. All
four are about the instants at which conversions happened, and the only instrument
that can observe an instant here is the witness. **The marker wire from CN7 pin 19
to the MCC 118 is not connected.**

Why no firmware number can stand in: any timebase on this die that could count the
sampling interval is derived from the same oscillator that defines it, so the part
would confirm a wrong rate exactly as readily as a right one. That is the whole
reason this file specifies an external witness.

## A second witness for criterion 1 only, Thursday 8 October 2026

**Appended, not edited.** Nothing above has changed since it was written before the
first run, and the four criteria stand exactly as they were.

This section exists because the status table above was written on the assumption
that the MCC 118 is the only instrument here, and that is not quite true. **The
host PC's clock is an independent timebase**, so counting blocks over a long
wall-clock interval measures the mean sample rate without asking the part to time
itself. The firmware already prints a running block count, so no firmware change is
needed.

**What it can settle: criterion 1, and nothing else.** The console sees block
completions, 64 samples apart. It cannot observe the interval between two
conversions, so criteria 2, 3 and 4 are untouched by it. Those three are the
chapter's actual subject and they still require the witness on PB4.

**Method, fixed before the first such run.** Read COM13 at 115200 on the win11
skyhorizon demo laptop, stamping each received report line with `Get-Date`. Take
the first and last stamped lines; divide the difference in `blocks` by the
difference in time; multiply by 64 samples per block. Run at least 600 seconds
after a single RESET press.

**Threshold, fixed before the first such run.** 999.0 to 1001.0 Hz passes.
Anything outside fails. That is criterion 1's 0.1 percent and not a looser figure
chosen to fit a coarse instrument.

**What refutes the instrument rather than the firmware.** Two consecutive 600
second runs disagreeing by more than 0.05 percent. In that case the number is
withdrawn rather than averaged, because an instrument that cannot repeat itself
cannot support a claim at one tenth of its own scatter.

**Why the window has to be that long.** A report line is emitted every 15 blocks,
which is 0.96 seconds at nominal. Over a short window the count is quantised to
whole lines, and that quantisation was large enough on Thursday 8 October 2026 to
yield 960.0 Hz and 1056.0 Hz from the same image in a single capture. Stamping the
lines removes the quantisation rather than shrinking it, and the long window
reduces what remains of the serial latency and clock drift to well under the
threshold.
