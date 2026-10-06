# Sampling on a timer at exactly 1 kHz

Status: c=links cpp=host python=host rust=host

Chapter 6 of the NUCLEO-H7A3ZI-Q firmware volume. Three ways to sample at
1 kHz, one application, and an external witness that decides which of them
actually does.

**Four implementations of the witness, proven equivalent on Sunday
4 October 2026**, which is the half of this project that can be proven. All four
agree on all nineteen fields of all nine captures, in WSL on the win11 skyhorizon
demo laptop, bing@JPTOUPM678, with cargo 1.99.0, gcc 15 and Python 3.14.4. The three back ends still refuse at
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

**Started Wednesday 30 September 2026.** What has been run is the host-side
analysis, against synthetic input, which is deliberately the first thing rather
than the last.

**And on Tuesday 6 October 2026 all three back ends compiled and linked for the
first time**, on win11 skyhorizon with CubeIDE's `arm-none-eabi-gcc` 14.3.1 and
the CMSIS pack passed as `CMSIS_DEVICE_DIR`. Nothing was flashed. They had never
been compiled by any machine before that: the three targets are gated behind that
pack, continuous integration passes none so it skips them, and the one laptop
with a pack had never been asked for them.

| target | FLASH | total |
|---|---|---|
| `p06-sampling-timer` | 9344 B | 21 417 B |
| `p06-sampling-systick` | 9460 B | 21 533 B |
| `p06-sampling-dma` | 9476 B | 21 293 B |

The DMA variant places a 256 byte `.dma_buffer` section, which is the placement
attribute doing its job and is the first evidence that it does.

**The status line above said `c=links` before any of them could link**, which is
worth stating plainly rather than quietly becoming true. Until commit 73 of the
same day the three targets were missing `c/instr/lseref.c`, which
`c/instr/freqcount.c` calls into for the crystal gate, so they would have failed
with three undefined symbols. The claim was aspirational for as long as it stood.

**No check in this repository could have caught that, and that is the gap worth
naming.** `python/tools/check_status.py` verifies that a language claiming any
state but `none` has a source file behind it; it cannot verify that the file
builds. So of the five states, `written` and below are checkable from the tree,
while `links` and `board` can only be established by a build and by a board.
`python/tools/check_link_closure.py` now catches the specific way these three
were broken, a source list not closed under its own includes, which is not the
same as checking that they link.

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

## What is deliberately not finished, item by item

Every back end returns a negative value from `acq_start` rather than running
with a guessed peripheral setting, and `main.c` prints which and stops.

**And here is the list, which was counted wrongly once before it was counted
properly.** A first pass counted `TO BE CONFIRMED` comments and concluded that
`acq_timer.c` had three items, all of them answerable from ST's pack. Reading
what the file *does* rather than what it says gives a different answer: it
configures TIM6 and enables `ADC_IRQn`, and it contains **no converter
configuration at all**. No clock enable, no regulator, no calibration, no
channel, no resolution, no trigger write, no conversion start. A comment count
is not a dependency list.

| what is needed | state as of Tuesday 6 October 2026 |
|---|---|
| the APB1 timer clock, so TIM6 divides to exactly 1 kHz | **sourced.** The TIMPRE rule from `stm32h7xx_hal_rcc_ex.h` lines 3596 to 3605, now in `c/instr/pwmmath.h` with the arithmetic and a host test |
| the marker pin, which is what any witness actually watches | **sourced.** PB4, CN7 pin 19, Arduino D25, from UM2408 Rev 6 Table 18 page 44, which is this board's own table and not the H745's beside it |
| the ADC external trigger value naming TIM6 TRGO | **sourced.** `EXTSEL` 13, from `stm32h7xx_ll_adc.h` line 993, cross-checked against TIM1 CH1 carrying no `EXTSEL` bits at all, which the field's table requires at position zero |
| the DMAMUX1 request number for the converter | **sourced.** 9, from `stm32h7xx_hal_dma.h` line 229. This one belongs to `acq_dma_double.c` |
| **the converter bring-up sequence** | **not sourced, and it gates all three back ends.** Deep power-down exit, the voltage regulator and its start-up wait, calibration, channel preselection, boost and clock mode, enable and the ready flag. ST's `stm32h7xx_hal_adc.c` implements all of it, so it is derivable from the pack the way the clock tree was, but it has not been read |
| the rising-edge enable beside `EXTSEL` | not sourced; it is the `EXTEN` field and one more line of the same LL header |

**So four of six are in hand and the fifth is the one that matters.** Three
sourced values cannot be written anywhere useful until the converter exists,
because `EXTSEL` selects a trigger for a peripheral that is not configured and
the timer clock drives a TRGO that nothing consumes. The honest order is the
bring-up first, then the three values, then a measurement.

### The converter bring-up: what reading ST's HAL established, and what it did not

`stm32h7xx_hal_adc.c` from STM32Cube_FW_H7_V1.13.0 was searched on Tuesday 6
October 2026 for the six fields that decide this. It is a transcription job from
ST's C into register writes, and the point of searching before writing is that
three of the findings are traps a clean transcription walks straight into.

| what the search found | why it matters |
|---|---|
| **`PCSEL` has two spellings**, at lines 2901 and 2905: `PCSEL_RES0` in one branch and `PCSEL` in the other | channel preselection is H7-specific, and which name applies to this part is a conditional in ST's own source rather than a fact about the family. Material written for another STM32 family has no such register at all, so a sequence copied from one reads as complete and samples nothing |
| **a silicon erratum workaround**, lines 3713 to 3716: "if `ADEN` is set less than 4 ADC clock cycles after the `ADCAL` bit ... continue setting `ADEN` until `ADRDY` becomes 1" | the obvious implementation, one write to `ADEN` then poll `ADRDY`, is WRONG. The enable has to be re-asserted inside the wait. A converter that starts most of the time is worse than one that refuses |
| **`ADCAL`, `ADDIS` and `ADEN` are "read-set"**, line 906 | they cannot be cleared by writing zero, so the read-modify-write habit that works on every other register here silently does nothing |
| **boost mode is its own function**, `ADC_ConfigureBoostMode` called at lines 781 and 785 | the setting depends on the ADC clock frequency, and this board's clock is the quantity seven explanations have been withdrawn for. The honest implementation computes it from `board_pclk2_hz()` and refuses outside the range it can justify, rather than hard-coding a value that happens to work |

### The sequence, complete and cited

Five further reads of the pack on Tuesday 6 October 2026 finished it. Every step
below carries the file and line it came from, in STM32Cube_FW_H7_V1.13.0.

| step | what to write | source |
|---|---|---|
| 1 | clear `DEEPPWD`, forcing the read-set bits to their reset state: `CR &= ~(DEEPPWD | ADC_CR_BITS_PROPERTY_RS)` | `stm32h7xx_ll_adc.h:6823` |
| 2 | set `ADVREGEN` the same way: `MODIFY_REG(CR, ADC_CR_BITS_PROPERTY_RS, ADVREGEN)` | `:6856` |
| 3 | wait **10 microseconds**. A delay, not a poll | `:1537`, `LL_ADC_DELAY_INTERNAL_REGUL_STAB_US`, which is `tADCVREG_STUP` |
| 4 | boost mode, from the ADC clock | `stm32h7xx_hal_adc.c:3939` to `:4008` |
| 5 | calibrate, then enable by **re-asserting `ADEN` until `ADRDY` reads 1** | `:3713` to `:3716`, an erratum workaround |
| 6 | `PCSEL \|= 1 << (channel & 0x1F)` | `:2905` |
| 7 | `EXTSEL` = 13 for TIM6 TRGO, plus the edge in `EXTEN` | `stm32h7xx_ll_adc.h:993` |
| 8 | the sequencer rank for the channel | `stm32h7xx_hal_adc.c:2910` |

**The read-set mask is seven bits**, from `stm32h7xx_hal_adc.c:366`: `ADCAL`,
`JADSTP`, `ADSTP`, `JADSTART`, `ADSTART`, `ADDIS` and `ADEN`. "Writing 0 has no
effect on the bit value." ST forces them to reset state on every write to `CR`
rather than read-modify-writing, and steps 1 and 2 have to do the same.

**The boost rule, and the input is not APB2.** An earlier note here said the
setting would be computed from `board_pclk2_hz()`. That was wrong.
`ADC_ConfigureBoostMode` branches at line 3942 on the clock mode: synchronous
takes **HCLK** divided by the `CKMODE` prescaler, asynchronous takes a different
clock entirely, `RCC_PERIPHCLK_ADC`. Then, for this part:

```
freq = adc_clk / 2
BOOST = 0  if freq <= 6 250 000
        1  if freq <= 12 500 000
        2  if freq <= 25 000 000
        3  otherwise
```

### Four forks, and why that count is the finding

Each of these would have produced code that compiles, links, flashes and samples
at the wrong rate or not at all. None was visible without reading the `#if` or
the caption.

| fork | resolved how |
|---|---|
| `PCSEL` or `PCSEL_RES0` | `PCSEL_RES0` sits behind `#if defined(ADC_VER_V5_V90)`, and that macro does not appear in `stm32h7a3xxq.h`. So plain `PCSEL` |
| the boost rule's two branches | `#if defined(ADC_VER_V5_3)`, and that macro **is** defined, at `stm32h7a3xxq.h:2621`. So the threshold table above |
| a silicon-revision dependency | the `HAL_GetREVID() <= REV_ID_Y` rule is in the `#else`, which this part never compiles. **There is no revision fork for the H7A3** |
| synchronous or asynchronous ADC clock | not a fork in ST's code but a configuration this project must choose and justify, and it decides which clock feeds the boost rule |

**That count is why this goes into a host-testable file before it goes into a
register.** Four points where correctness depends on part identity, in a
repository whose thesis is that material written for the STM32H7 family is not
material written for this part. So the shape is the one `clocktree.c` and
`pwmmath.c` already use: the four decisions and the boost arithmetic as pure
functions of their inputs, reading no register, driven from a table of cases on
the authoring laptop, and then the register writes calling into that and refusing
on anything the table cannot answer.

**What is still a choice rather than a fact**: the clock mode, the channel and
its sampling time, and the resolution. Those are this project's to decide and to
justify against the 1 kHz the chapter is about, not ST's to tell us.

**What the three images do today** is compile, link, start, print which value is
unconfirmed, and stop. That is the refusal path working, and it is the correct
behaviour for a project whose central claim is that a clean square wave at the
wrong rate is indistinguishable from a right one without an external witness.

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
