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
the full aggregate rate is available. The marker toggles once per sample, so at
1 kHz it is a 500 Hz square wave carrying 1000 edges per second, with about 100
samples between one edge and the next. That is ample for an edge time to about
one sample period, which is 10 microseconds. It is also why the marker must
toggle rather than pulse: a pulse of two consecutive stores is high for tens of
nanoseconds, and a 10 microsecond sampler sees it only in fragments, which is
what the first capture found on Friday 9 October 2026, recorded at the end of this
file.

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

- **Counting.** Number of edges, rising and falling alike, divided by the time
  between the first and last. Robust, and insensitive to any single edge being
  mistimed.
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

## Criterion 1 has an answer, and it is a failure, Thursday 8 October 2026

**Appended, not edited.** The four criteria above are untouched. This records what
the second witness returned, what had to be fixed in it first, and what the answer
means.

### The validity check the method above should have carried, and did not

The section before this one fixed a threshold for the answer without fixing any
check that the instrument was working. **Three runs were taken and all three were
invalid**, and the method as published would have accepted any of them:

| run | lines | blocks span the lines require | span observed | rate it reported |
|---|---|---|---|---|
| 1 | 743 | 11 145 | 9465 | 946.782 Hz |
| 2 | 706 | 10 590 | 90 | 9.011 Hz |
| 3 | 772 | 11 580 | 13 005 | 1301.394 Hz |

A report line is emitted every 15 blocks, so the line count and the block span are
two routes to the same number and they must agree. **In all three runs they did
not**, and that disagreement was visible in the data before any rate was computed.
The published method did not look.

**One mechanism explains all three.** Each run's first row sat exactly 15 blocks
past the previous run's last row: 9780 then 9795, 9885 then 9900. That is a line
the serial driver buffered while the port was closed, delivered on reopening and
stamped with a fresh arrival time. Its `blocks` value is old and its timestamp is
new, so any span starting there is wrong, and wrong in whichever direction the
backlog happens to fall. Runs 1 and 2 also carried a restart, because the
instruction at the time was to press RESET after starting the reader.

**The check that is now part of the method, and is required before any number is
reported.**

1. Count every step between consecutive rows that is not exactly 15 blocks.
2. Count banner lines, which prove a restart.
3. Measure only the longest run of consecutive rows with no bad step, and report
   nothing at all if that run is shorter than 600 seconds.
4. Do not press RESET. A rate measurement wants an uninterrupted stretch.

### The run that passed its own check

    rows 813  banners seen 0  steps not equal to 15: 1
    longest clean segment rows 82 to 812, blocks 24450 to 35400, 698.831 s
    mean rate 1,002.818 Hz   deviation 0.2818 percent   FAIL

**The segment is internally exact.** 730 steps at 15 blocks each requires a span of
10 950, and the observed span is 10 950, a difference of zero. 700 800 conversions
over 698.831 seconds. No banner, so no restart. The single bad step is where the
82 line backlog meets the live stream, and the clean-segment rule discarded the
backlog, which is the job it was added for.

### Criterion 1: FAIL

**1002.818 Hz, which is +2818 parts per million against a threshold of plus or
minus 1000.** Failed by a factor of 2.8. The threshold was published before the
run and is not being revisited now that the answer is in.

### The cause is the oscillator, and a second instrument already said so

TIM6 runs from the APB1 timer clock, which the firmware's own banner reports as
64 000 000 Hz with the prescaler at one and `TIMPRE` clear, so the divisors were
computed for exactly 64 MHz.

`docs/the-board-and-the-wiring.md`, in the section "The internal oscillator,
nominal 64 MHz", records that oscillator as **always above nominal, 2630 to 3257
parts per million high**, measured by the on-board gated counter against the
real-time clock crystal's sub-second register. That is a different instrument from
this one and neither of them is the oscillator under test.

If the divisors assume exactly 64 MHz and the oscillator is 2630 to 3257 ppm fast,
the rate must land between **1002.630 and 1003.257 Hz**. It measured 1002.818 Hz.
**Inside the band.**

The sign was predicted as well, by the opposite-sign table in the same document: at
the reset clock a requested delay comes out 3027 ppm **short**, so a requested 1000
microsecond period is short and the rate is high. It is high.

A host clock cannot account for this. An unsynchronised PC crystal is tens of parts
per million and a synchronised one far less, where this is 2818.

### What that means for the claim, stated as options rather than as a fix

**"Exactly 1000.0 Hz" is not reachable on the internal oscillator**, by about a
factor of 2.8 over this file's own criterion, and no amount of firmware care
changes it. Four options, not equivalent, and the first of them is withdrawn:

1. ~~Run the clock tree from the 8 MHz bypass.~~ **Withdrawn the same day it was
   proposed**, and the evidence against it was already in this repository when it
   was written. `c/board/stm32h7a3_regs.h`, in the `HSE_HZ_BYPASS` comment, records
   five readings of that source derived from core measurements against the
   32.768 kHz crystal: about 7990652, 7983868, 7991850, 7989549 and 7989285 Hz,
   which is **1019 to 2016 parts per million LOW and a scatter of 998**. The
   internal oscillator's scatter is 627. **The external source is worse**, by about
   a factor of 1.6, so this would fail criterion 1 on stability as well as on
   offset. That comment already draws the conclusion: a crystal-derived 8 MHz does
   not scatter by a part in a thousand on a bench at room temperature, while an RC
   oscillator does and so does a clock synthesised from one. Calling it
   "crystal-referenced" was the error, and it was mine.
2. **Restate the claim as nominal**, with the oscillator's measured tolerance
   quoted beside it. **This is now the realistic default.**
3. **Keep the claim and publish it as failed on this clock**, which is honest and
   leaves the chapter without the result it was built for.
4. **Change the target to 1024 Hz and drive it from the 32.768 kHz crystal**, which
   is the only route on this board that could pass a 0.1 per cent criterion. The
   crystal is the one component here with crystal accuracy and it is what every
   clock figure in this volume has been measured against. The arithmetic decides it:
   1000 Hz needs a divisor of 32.768 and **1024 Hz needs exactly 32**. Two real
   costs. TIM6 sits on APB1 and cannot be clocked from that crystal, so the
   mechanism would have to change to a low-power timer, and **whether such a timer
   can drive the converter's external trigger is an RM0455 question and RM0455 is
   unread.** So this is a research item and not a plan.

**The stronger statement that follows, and it is the repository's own evidence
rather than a preference:** "exactly 1000.0 Hz within 0.1 per cent" is not reachable
from **any** source on this board. The internal oscillator is 2630 to 3257 ppm high,
the external one is 1019 to 2016 ppm low with a worse scatter, and the only accurate
source does not divide to 1000.

**One thing cuts the other way and is worth saying.** This file records the
MCC 118's own clock accuracy as an open question against its user guide, and the
32.768 kHz crystal is of untraceable accuracy. A network-synchronised host clock is
good to tens of parts per million over ten minutes, which makes the console run very
likely **the most accurate frequency measurement this volume has made**, and better
than the witness meant to supersede it. The 1002.818 Hz figure is not the weak link.

### Criteria 2, 3 and 4 are untouched and this changes nothing about them

The interval spread, the worst case interval and dropped or doubled edges are all
about the instants of individual conversions. This instrument sees block
completions, 64 samples apart, and cannot observe an interval. **The MCC 118 on PB4
at CN7 pin 19 remains the only route to three of the four**, and those three are
what the chapter is actually about.

### The board is shared, so attribution is part of the method

Added Thursday 8 October 2026, after the measurement above was taken. Two projects
use this one NUCLEO-H7A3ZI-Q, so the image running at any moment is whatever the
last session flashed. **A capture taken without reflashing first is a capture of an
unknown image.** From here on, reflash and capture are consecutive steps with
nothing between them.

**The accepted run above was checked against this rather than assumed**, because it
was taken some time after its flash. Three things attribute it to the right image.
Only `projects/P06-timer-sampling/c/main.c` prints a `seq ... blocks ...` line
anywhere in this repository, and only its current version prints the `convovr`
field, which appears on every line of that run. `banners seen 0` rules out a reset
during the window, and a reflash resets the part. And the block counter ran
continuously through the window, with the one discontinuity accounted for as the
serial backlog. **The measurement stands.**

**The validity check specified above already covers this case, unchanged.** It was
written to detect a restart, and a reflash by another session is a restart. That is
worth noting because it is the second time that check has caught something it was
not designed for, the first being the stale buffered line.

## The witness is live, and the first capture refuted the marker, Friday 9 October 2026

**The chain ran end to end for the first time, and the first thing it measured was
a defect in the firmware's marker.** That is what a shakedown is for, and it is
recorded in the order it happened.

### Bringing the witness up, and one fault that was not in the wiring

The `daqhats` shared library was built from MCC's repository on the Raspberry Pi 4,
version 1.5.0.1 with libgpiod 2, and the Python wrapper came from piwheels at
1.4.1.0, which is what MCC's own README installs. `hat_list` then reported the HAT
at address 0, and that proved less than it reads as proving: `hat_list` returns the
EEPROM record `install.sh` wrote under `/etc/daqhats` and does not touch the bus.
The first real open, `mcc118(0)`, failed with `Board not responding`, as `bing` and
as root alike.

**The cause was two lines in `/boot/firmware/config.txt`, 61 and 62, giving both
SPI0 chip selects to CAN controller overlays** from the other volume this Raspberry
Pi serves: `mcp251xfd` on `spi0-1` and `mcp2515` on `spi0-0`. Each overlay replaces
the `spidev` node on its chip select, so `/dev/spidev*` did not exist even with
`dtparam=spi=on` on line 59, and `dmesg` showed both CAN drivers failing to find
their chips at boot. A failed probe does not hand the chip select back. Both lines
are commented out, the file as found is kept beside it as
`config.txt.before-mcc118-20261009`, and **the other volume needs those two lines
back before its CAN work runs again.** After a reboot `/dev/spidev0.0` and
`/dev/spidev0.1` exist and the HAT answers: serial `DBA2019`, firmware 1.03,
bootloader 1.01.

### The capture was attributable, by the stronger route this time

The timer image was copied to the probe's disk and the console opened the moment
the copy returned, before the part had finished programming, so the banner could
not be missed: `image 15196 B`, `TIM6 TRGO on the rising edge`, calibration 181
polls, enable 4 polls, `seq` restarting at 15. An earlier attempt the same day
opened the port some seconds after the copy and saw only reports from `seq 240`
on, attributable by the line format and `convovr 0` but not by the flash figure.
Copy and open as one step is the method from here.

### The shakedown, two seconds at 100 kS/s

    199784 samples at 100000.0 Hz
    duration            1.998 s, 102 rising edges
    rate, fitted        50.6662 +/- 0.2772 Hz
    rate, counted       51.0284 Hz
    interval spread     13165.96 us standard deviation
    worst interval      65809.27 us
    missing edges       1884
    FAIL on criteria 1 to 4, the two routes agree, VERDICT: FAIL

**50.67 Hz is not a 1 kHz marker seen badly, and it is not mains either**, which
was the first reading and was tested before it was kept; the statistic below says
what it is.

### The cause: the marker was a pulse the witness cannot see

`marker_pulse()` was two consecutive stores to `BSRR`, a set and a clear, so the
pin was high for a few core cycles at 64 MHz: **tens of nanoseconds.** The witness
samples every **10 microseconds.** The arithmetic needs no instrument: a sampler
with a 10 microsecond period sees essentially none of a 50 nanosecond pulse, and
the input otherwise sits at a driven 0 V.

### The statistic that tested the cause, run before anything was committed

Prediction written first: median within 20 mV of zero, standard deviation of a few
millivolts, fewer than 0.1 percent of samples above 1 V. The 199 784 voltages:

    min -0.014205  p1 -0.014205  median -0.009094  p99 -0.009094  max 0.491787
    mean -0.00903  sd 0.013121
    above 1.0 V: 0    above 0.1 V: 346

**The pin is at a driven 0 V**: the first and ninety-ninth percentiles are two
adjacent converter codes, 5.1 mV apart, and nothing reaches 1 V. **And the first
reading of the 50.67 Hz, mains hum, was wrong**: hum of a few millivolts cannot
cross a threshold at 0.34 V, and 346 samples sit between 0.1 V and 0.49 V on a
floor that is otherwise two codes wide. Those are the sampler catching part of the
pulse. With thresholds set at 30 and 70 percent of the observed swing, 0.14 V and
0.34 V, the 102 "edges" are the catches that cleared 0.34 V, randomly placed on
the 10 microsecond grid, which is why the interval spread is 13 ms on a 19.6 ms
mean: the statistics of random catches, not of a clock. 50.67 Hz against 50 Hz
mains was a coincidence, and the amplitude column is what exposed it.

One inference from the same numbers, offered with its basis: 346 catches in about
2005 pulses is one in six, where a 50 ns pulse on a 10 microsecond grid would give
one in two hundred, and the catches peak at 15 percent of 3.3 V. Both fit the HAT's
input low-pass stretching the pulse to roughly two microseconds while flattening
it; the user guide's input bandwidth, still unread, would say whether that holds.

And the record had said so all along without the code following it: `marker.c`
line 3 says "Toggled once per sample", this file says "toggles one pin once per
sample", and the handler comment in `acq_timer.c` says "one edge per CONVERSION".
A pulse is two edges per conversion and a toggle is one. **The code was the
outlier against three statements of intent.** The chapters that plan a Nordic PPK2
digital input, which samples at the same 100 kS/s, carry the same pulse and the
same arithmetic applies to them.

### The fix, and what it does not change

- `marker_toggle()` replaces `marker_pulse()`: one store, alternating set and
  reset, the level kept in a static so the port is never read. `marker_init`
  leaves the pin low and the static starts at zero, so the first call is the first
  rising edge.
- All four analysers count **both polarities**, with the Schmitt pair as the
  hysteresis: a rise counts only from an established low, a fall only from an
  established high. The function is `marker_edges` in Python, C++ and Rust and
  `rate_marker_edges` in C. The interpolation across the midpoint is unchanged, so
  the four still have to agree to one part in 1e12.
- Both synthesisers render a toggle rather than a pulse per edge. The Python suite
  is unchanged in count, 204 passing and 146 skipping on win11 aquamarine, and the
  mutation was run: the old rising-only detector on the new synthesiser reads
  **500.000 Hz and FAIL**, the new one 1000.000 Hz and pass.
- **Nothing in the four criteria changes.** The nominal is 1000 edges per second
  as before; the pin now carries them as a 500 Hz square wave.
- The C, C++ and Rust analysers are edited and not yet compiled; they are checked
  only in WSL on win11 skyhorizon. The pre-existing cap of 4096 edges in those
  three, about four seconds at 1 kHz, is unchanged; the analysis on the Raspberry
  Pi is `rate.py`, which has no cap.

### What the shakedown established despite its verdict

The HAT scans one channel at 100 kS/s for two seconds without overrun, `scan.py`
and `rate.py` run on the Raspberry Pi from the clone, the two files land, and the
verdict machinery refuses on garbage rather than passing it. The witness is live;
what it watched was wrong.

### Status of each criterion, Friday 9 October 2026

| Criterion | State |
|---|---|
| 1. Mean rate within 0.1 percent | **FAIL**, +2818 ppm, by the host clock on Thursday 8 October 2026 |
| 2. Interval spread below 10 microseconds | **not evaluated**; the first capture is invalid for the reason above |
| 3. No single interval more than 50 microseconds off | **not evaluated**, same |
| 4. Zero dropped or doubled edges | **not evaluated**, same |

### Prediction for the next shakedown, written before it runs

With the toggling image flashed and the console confirming it, two seconds should
give about **2005 edges at about 1002.8 edges per second**, the host clock's figure
of Thursday 8 October 2026 within the few tens of ppm two independent clocks can
disagree by, so criterion 1 fails again by about 2818 ppm, and criterion 4 reads
zero missing edges. No prediction is made for criteria 2 and 3 beyond the README's,
that the timer build should do well on spread because its sampling instant is made
in hardware and its edge carries only the handler's latency.

What would refute the chain rather than the firmware: a swing under a volt with a
rate far below 1000 means the pin is not swinging at the witness, which is the
wire on PB4 at CN7 pin 19, the ground wire, or an image that still pulses, in that
order; about 2005 edges per second means something still makes two edges per
conversion; exactly 1000.0 means one of the two clocks is wrong.

## The first valid capture: three criteria pass, one fails, Friday 9 October 2026

The toggling timer image, `image 15220 B`, flashed with the console opened as the
copy returned and the banner caught; two seconds on the MCC 118 at 100 kS/s,
199 770 samples, `run-002`; analysed with `rate.py` on the Raspberry Pi:

    duration            1.998 s, 2003 edges
    rate, fitted        1002.6204 +/- 0.0002 Hz
    rate, counted       1002.6142 Hz
    interval mean       997.39 us
    interval spread     3.39 us standard deviation
    worst interval      990.28 us, 9.72 us from nominal
    missing edges       0
    FAIL  rate_within_tolerance
    pass  jitter_within_limit
    pass  worst_interval_within_limit
    pass  no_missing_edges
    pass  routes_agree
    note: measured spread is below half the witness sample period, so it is
          limited by the instrument and not by the board

### Status of each criterion, Friday 9 October 2026, after the first valid capture

| Criterion | State |
|---|---|
| 1. Mean rate within 0.1 percent | **FAIL**, +2620 ppm by the witness today; +2818 ppm by the host clock on Thursday 8 October 2026 |
| 2. Interval spread below 10 microseconds | **pass**, 3.39 µs, and that figure is the instrument's floor rather than the board's: it sits below half the 10 µs sample period, exactly the case this file said criterion 2 would survive and criterion 3 would not resolve |
| 3. No single interval more than 50 microseconds off | **pass**, 9.72 µs, from 2003 intervals |
| 4. Zero dropped or doubled edges | **pass**, 2003 edges, none missing |

Two seconds is a shakedown and the specification says 60 seconds, three runs per
build. The 60 second runs follow in the next section; the two second figures are
recorded because they are the first readings criteria 2, 3 and 4 have ever had.

### The predictions, scored

Written in the previous section before the run. **Held:** about 2005 edges in two
seconds, 2003 arrived; criterion 1 failing; zero missing edges; the timer build
doing well on spread, which the README predicted because its sampling instant is
made in hardware and the marker edge carries only the handler's latency.

**Missed, and the miss is the finding:** the rate was predicted at about 1002.8 Hz,
"within the few tens of ppm two independent clocks can disagree by". It read
1002.62 Hz, which is **198 ppm** below the host-clock figure of 1002.818 from
Thursday 8 October 2026,
an order of magnitude more than the prediction allowed. Two readings are open and
the two second run cannot separate them:

- **the instruments disagree**: the HAT's own clock against a network-disciplined
  host clock, and the HAT's clock accuracy is still unread in its user guide;
- **the oscillator moved**: the HSI is an RC oscillator and 200 ppm between
  Thursday 8 October 2026 and Friday 9 October 2026 is well inside what one does
  with temperature.

What separates them is a window in which both instruments watch the same image at
the same time, which the 60 second run provides: the stamped console runs for 640
seconds and the HAT captures 60 of them. Agreement within about 20 ppm means the
HSI moved between the days; a second 200 ppm gap means the instruments, and the
HAT's clock specification becomes the next thing to read.

### What the toggle cost, and one label

`marker_toggle` is 24 bytes of flash more than the pulse in both images that call
it, 15 196 to 15 220 for the timer and 14 396 to 14 420 for the tick build, and the
size table in `docs/building.md` carries both as a second dated column. `rate.py`
printed "2003 rising edges" while counting both polarities; the label now says
"edges, both polarities", since a report line that misnames what it counted is
the kind of line this file exists to prevent.

## The 60 second run, twice, with the host clock alongside, Friday 9 October 2026

### Two 60 second captures, and the first was overwritten

The first 60 second run on the toggling timer image, taken between the reflash at
15:02 and the one at 15:17, read **60 168 edges, 1002.9058 ± 0.0001 Hz, spread
3.93 µs, worst interval 659.94 µs (340.06 µs off), one missing edge**, failing
criteria 1, 3 and 4. The second, taken from 15:17:33 to 15:18:33 after the reflash
at 15:17:17, read **60 165 edges, 1002.8038 ± 0.0001 Hz, spread 3.52 µs, worst
9.81 µs, zero missing**, failing criterion 1 alone.

**The first run's file no longer exists.** The second was started with the same
`--out run-003` and `scan.py` replaced the CSV and JSON without a word, so the one
interval more than 50 µs off nominal seen that day survives only in the pasted
report above, and the edge listing meant to show its neighbours ran on the second
file and found nothing to list. A 660 µs interval has `k` of 0.66, which the
missing-edge rule counts once as a doubled edge; a 1337 µs partner would have
rounded to 1 and gone uncounted, so the report is consistent with one marker edge
about 340 µs late and equally with one extra edge, and the data that would have
said which is gone. It is recorded as seen and marked not reproducible.
`scan.py` now refuses an existing output by name, with a test that drives it to
that refusal on a laptop with no HAT.

### The host clock over the same window, and what its own jitter is worth

The stamped console ran from the 15:17:17 reflash for 640 s: **700 lines, one
banner, 667 reports from `seq 15` to `seq 10005`, every step exactly 15**, so the
run is valid by the check this file specifies. First report 15:17:19.982 at 15
blocks, last 15:27:57.478 at 10 005 blocks: **1002.924 Hz** over 637.5 s. The
second HAT run sits inside that window, 15:17:33 to 15:18:33.

**Before comparing the two, the host method's resolution was measured rather than
assumed, and the assumption was wrong by a factor of thirty.** The firmware emits a
report every 15 blocks at a rate that moves by parts per million, so the scatter of
the stamp intervals is the stamp jitter: 666 intervals, mean 957.2 ms, **standard
deviation 28.3 ms**, 891 to 1017. A rate over 637 s therefore carries about
**44 ppm** of stamp noise, a rate over 60 s about **470 ppm**. The per-minute
console rates, 1001.7 to 1004.6 Hz, are that noise and say nothing about the
oscillator; a prediction made on the assumption that they resolved 35 ppm could
not have been decided by them either way, and was not.

**That withdraws a sentence written on Thursday 8 October 2026.** The 698 s console
run was called "very likely the most accurate frequency measurement this volume has
made, and better than the witness meant to supersede it". The witness resolves a
rate to 0.0001 Hz, 0.1 ppm, in two seconds; the host method resolves 44 ppm in ten
minutes. The host's merit is a traceable clock, not precision, and the 1002.818 Hz of Thursday 8 October 2026 carries the same ±44 ppm as today's 1002.924, which makes their 106 ppm
difference consistent with noise.

### What the comparison does and does not settle

Host 1002.924 ± 44 ppm over ten minutes; HAT 1002.804 ± 0.1 ppm over the first
minute of those ten. **120 ppm apart, 2.7 standard deviations of the host method:
not agreement, and not a demonstrated offset.** Two readings remain, and the host
cannot separate them because the thing that would separate them, the oscillator's
movement inside the ten minutes after a reset, is below 470 ppm per minute:

- the instruments differ by about 100 ppm, with the HAT's clock accuracy still
  unread in its user guide;
- the oscillator moved within the window, as an RC oscillator does while a die
  warms after a reset, and the HAT's first minute is not the host's ten.

**The HAT's own three readings today, 1002.620, 1002.906 and 1002.804, spread
285 ppm with 0.1 ppm statistical uncertainty each, and that spread is real.** It
belongs to the oscillator or to the HAT's clock, and the measurement that
separates those needs no host at all: two 60 second HAT captures in one powered
window, one in the first minute after a reset and one eight minutes later. If the
HAT reports the oscillator moving, the spread is the oscillator; if it reports the
same figure to a few ppm, the spread is between powered windows or in the HAT.
That is the next measurement on criterion 1, and it is cheap.

**None of it moves the verdict.** Every instrument on every run puts the rate
between 1002.6 and 1002.9 Hz, 2600 to 2900 ppm above the claim, against a
tolerance of 1000. Criterion 1 fails by every route.

### Status of each criterion after the 60 second run

| Criterion | State |
|---|---|
| 1. Mean rate within 0.1 percent | **FAIL** on every run and every instrument, +2620 to +2924 ppm |
| 2. Interval spread below 10 microseconds | **pass**, 3.52 µs over 60 165 intervals, the instrument's floor |
| 3. No single interval more than 50 microseconds off | **pass** on the valid 60 s run, 9.81 µs; the overwritten run reported one 340 µs excursion, recorded and not reproducible |
| 4. Zero dropped or doubled edges | **pass** on the valid 60 s run; one counted on the overwritten run, same note |

### The predictions, scored

"Within a few ppm of today's 1002.62 Hz" for the 60 s HAT run: **missed**, it read
1002.804 and 1002.906, and the miss is the 285 ppm spread above. "Within about
20 ppm of each other" for host against HAT: **missed**, 120 ppm, and the number
was quoted before the host method's resolution had been measured. "The
HAT-window minute near 1002.80 and later minutes climbing" for the per-minute
console figures: **undecidable**, the per-minute resolution is 470 ppm. The lesson
is the one this volume already carries for the cycle counter: characterise the
instrument before predicting with it, and never state a resolution that has not
been measured.

### The console is shared too

The first two attempts at the stamped console were refused with `Access to the
port 'COM13' is denied`: another reader held the port, which on a laptop serving
two volumes is the other volume's console. The copy had already gone through both
times, so the board was reflashed twice for nothing. A denied port is the harmless
form of that collision; the harmful form is two readers draining one port and each
seeing half the lines, which "every step exactly 15" is there to catch. The
attribution section above covered the flash and not the port, and now it covers
both: one reader on COM13 at a time, and the stamped file's step histogram is the
proof there was.

## The tick build through the same witness, and the comparison the chapter exists for, Friday 9 October 2026

The systick image, `image 14420 B`, flashed with the console opened as the copy
returned, banner caught, `started by software, one conversion per tick`, `convovr
-1`, `seq` from 15.

### Two seconds: indistinguishable from the timer build at this witness

    2003 edges   1002.7771 +/- 0.0004 Hz   spread 3.51 us   worst 9.73 us off
    missing 0    criteria 2, 3, 4 pass, 1 fails

The timer build's own two seconds read 3.39 µs and 9.72 µs. Both figures sit at the
witness's floor, below half its 10 µs sample period, which was the second of the two
outcomes written before the run: **at this resolution the witness cannot tell a
hardware trigger from a software one on this board.** That is the chapter's
comparison on criterion 2, and it is a null result with a stated reason, which is a
result.

### Sixty seconds, first attempt: one 1.47 s gap, and it was the probe, not the tick

    58565 edges   fitted 973.62 Hz   spread 6093 us   worst 1470925 us
    missing 1471  criteria 1, 2, 3, 4 fail

One interval of 1.470 925 s among 58 564 is the whole of that spread: a single
outlier of that size contributes 1.47 s / √58 564 = 6.08 ms, which is the figure
reported. So this is one event and not jitter, and its shape is in the file:

    interval 14120:  454.4 us   from 14.0821 s      the edge before the hold came early
    interval 14121:  1470925 us from 14.0826 s      the pin DRIVEN high, 3.24 V steady
    interval 14122:  117381 us  from 15.5535 s      then low, no edges, 117 ms
    50 edges in the 50 ms before; 0 in the 50 ms after; then 998.9, 999.9, 1000.0 us

Each feature excludes something. A SysTick toggle cannot arrive 454 µs after its
predecessor unless the one before it was late by about 546 µs, so something held
the handler off briefly just before. A pin in reset floats and cannot hold 3.24 V for
1.47 s on a HAT input, so the core was running or halted with the GPIO driving
high. A stalled `EOC` wait, the one unbounded loop in this image, would hold the
level but could not make the earlier edge early, and once it ended the next pending
tick would toggle within a millisecond, not after 117 ms of silence. And 117 ms of
low followed by a rise is what a reset gives: `marker_init` drives the pin low,
twelve banner lines go out at 115200, the bring-up runs, and the first tick's toggle
is a rise because the static starts at zero.

**Together they are the signature of a debugger session on the SWD port**: a brief
halt as the probe connects, the core held halted for 1.47 s with the GPIO keeping
its last level, a reset, and the image booting. The image that booted toggles PB4 at
a kilohertz, and the other volume's two Nucleo applications drive no PB4 and no
marker, so it was a P06 image: either this one, after a connect, read and reset
without reprogramming, or a second copy of the same file. **Who held the port at
15:55:56 local is the one open line**; nothing in this session did. The console was
not stamping during that run, which is a gap in the method rather than in the
firmware and is closed below.

### Sixty seconds, second attempt, with the host stamping and nothing else on the board

    60164 edges   1002.8183 +/- 0.0001 Hz   spread 3.51 us   worst 9.79 us off
    missing 0     criteria 2, 3, 4 pass, 1 fails
    console: 134 reports, one banner, every step 15, no gap over 130 s
    captured 16:09:36 to 16:10:36 local by the Raspberry Pi's stamp, inside the
    console window of 16:08:56 to 16:11:06

The clean run, attributed to its console window by timestamp rather than by the
order the commands were typed. The timer build's valid 60 s read 3.52 µs and 9.81 µs; the two
builds are the same number at this witness over a minute as over two seconds.

### Status of each criterion, both builds, Friday 9 October 2026

| Criterion | timer | systick |
|---|---|---|
| 1. Mean rate within 0.1 percent | **FAIL**, 1002.804 Hz | **FAIL**, 1002.818 Hz |
| 2. Interval spread below 10 microseconds | **pass**, 3.52 µs, instrument floor | **pass**, 3.51 µs, instrument floor |
| 3. No single interval more than 50 microseconds off | **pass**, 9.81 µs | **pass**, 9.79 µs |
| 4. Zero dropped or doubled edges | **pass**, 0 of 60 165 | **pass**, 0 of 60 164 |

Three runs per build is what the procedure specifies and one valid 60 s run per
build is what exists; the table says one.

### The rule the day earned

A capture on a shared board is valid only if nothing else touched the board for its
whole length, and the only witness to that is the stamped console running alongside
it: a second banner is a reset, a pause in `seq` with no banner is the handler, and
an unbroken `seq` with a silent pin is the marker alone. From here on the stamped
console runs beside every capture, long or short, and the other session on win11
skyhorizon is asked to be quiet for the length of it. It is item 12 of the pre-flight
list.

### The predictions, scored

"Criterion 1 fails at about the same rate": **held**, 1002.78 and 1002.82 against the
timer's 1002.62 to 1002.91. "At a 10 µs floor the two builds may be
indistinguishable on criterion 2": **held**, and it is the chapter's result for now.
"Where a software instant shows itself, if at all, is criterion 3 or 4 over sixty
seconds, as a late edge and a short interval after it": the first 60 s run showed
exactly a late edge, a hold and a short interval, and **it was the probe**, which the
prediction did not list; the clean run shows nothing. The README's standing
prediction that the tick build loses on spread and worst case is **not decided**:
this witness cannot see the quantity it is about.
