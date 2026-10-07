# Sampling on a timer at exactly 1 kHz

Status: c=board cpp=host python=host rust=host

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

## The first three steps, on the board, Wednesday 7 October 2026

`p06-sampling-timer` was built, flashed and run on the NUCLEO-H7A3ZI-Q on the
win11 skyhorizon demo laptop. **The first time anything in this project has
reached the converter.** FLASH went from 9476 to 11468 bytes. Five captures over
COM13 at 115200, one of them from a single clean RESET press, all byte identical:

    nucleo-h7a3-sampling
      acquisition   timer
      nominal rate  1000 Hz
      instrument    core cycle counter
      tick rate     64000000 Hz
      tim6 clock            64000000 Hz  (pclk1 64000000, /1, timpre 0)
      adc leave deep power-down  ok       wrote 00000000  read back 00000000
      adc regulator enable       ok       wrote 10000000  read back 10000000
      adc regulator start-up     ok       wrote 0000000A  read back 10000000
      adc boost mode             REFUSED  the clock mode is unchosen
    acq_start failed: -7

**Every line was predicted before the flash**, including the refusal and its
code, which is the practice this volume adopted on Tuesday 6 October 2026: state
the threshold first so the answer cannot be interpreted to fit.

### What it settled

| Settled | On what evidence |
|---|---|
| The timer clock is **64000000 Hz** on the reset clock, with `CDPPRE1` dividing by one and `TIMPRE` clear | `pwmmath_timer_hz` answering from the decoded bus clock. This block returned `-2` unconditionally until Wednesday 7 October 2026 |
| `ADVREGEN` is **bit 28**, `0x10000000` | Written and read back identically |
| **`RCC_AHB1ENR_ADC12EN` is the correct bus clock enable** | The same argument that confirmed `RCC_APB2ENR` at 0x150: a wrong enable means no ADC register write sticks, and two of them stuck |
| The regulator **holds across the ten microsecond wait** | `CR` still reads `0x10000000` after it |
| The ten microsecond wait is **measurable on the cycle counter** at 64 MHz | 640 ticks, and the step reports ok rather than refusing for want of a rate |

### The notable non-event

**The ADC regulator accepted a direct write from reset.** It was worth watching
for the opposite, because the voltage scaling on Sunday 4 October 2026 did the
reverse: scale 0 is reachable only from scale 1, and the regulator declined a
direct write **in silence**, with no error and simply no ready flag. The
analogous trap was looked for here and did not occur. An absence that was
specifically tested for is worth recording, because the next reader would
otherwise have to look for it again.

### One defect in the report itself, which the capture exposed

`leave deep power-down` printed `wrote 00000000 read back 00000000`, and that
is consistent with **two different stories**: `DEEPPWD` set at reset and cleared
correctly, or `DEEPPWD` already clear and the write changing nothing. The step
does not print the as-found value first, so **this run cannot claim that deep
power-down was exited**, only that the bit reads clear afterwards.

That is the same lesson `c/board/clock280.c` already learned, which prints every
register before touching any of them and had its dump relabelled "as found"
after a RESET press turned out not to produce reset values.

**That fix is in as of Wednesday 7 October 2026**, later the same day: the
control register is printed before anything touches it, with `DEEPPWD` and
`ADVREGEN` broken out, so the next capture can say which of the two stories is
true. It has not been flashed, so the claim above still stands as written.

## Steps 4 and 5, written and built, not yet run

Later on Wednesday 7 October 2026, so **the capture above predates them.** That
distinction is the point of this section: the code that ran had three steps, the
code in the repository now has five, and only a build stands behind the two new
ones.

| | State |
|---|---|
| Steps 1 to 3 | Flashed and observed, five identical captures |
| Steps 4 and 5 | **Compiled and linked only**, `arm-none-eabi-gcc` 14.3.1 on win11 skyhorizon, FLASH 11468 to 12740 bytes |
| Step 6 | Refuses, and the reason changed |

### The clock mode, which was a decision and is now taken

Step 4 was blocked not by an unread register but by a choice nobody had made, and
ST does not make it either: `ADC_ConfigureBoostMode` branches on it at line 3942.
**Synchronous, `CKMODE` dividing by four**, written as two constants in
`acq_timer.c` so revisiting it is a two line edit.

Three reasons, and the cost named. It adds no register this volume has not
sourced, where the asynchronous route needs a dedicated kernel clock configured
and sourced first, which is a second unread question stacked on the first. Divide
by four is the **slowest** ratio `CKMODE` can express, which is the most
conservative choice available while this part's maximum ADC kernel clock remains
a datasheet question. And it yields a clock `adcmath` accepts rather than refuses.

**The cost is conversion rate**: 16 MHz at the reset clock where divide by one
would give 64, so a conversion takes four times as long as it need. For 1 kHz
that is irrelevant by three decades, and when it stops being irrelevant the fix
is to read the datasheet rather than to raise the number hopefully.

The field value was **read rather than inferred from the bit count**, and the
expectation was written down before the header was opened:
`LL_ADC_CLOCK_SYNC_PCLK_DIV4` is `(ADC_CCR_CKMODE_1 | ADC_CCR_CKMODE_0)`, both
bits of a two bit field at bit 16, with asynchronous as 0. The header agreed,
which is the only reason a literal 3 appears in the file.

### The enable, where the obvious implementation is wrong

From `stm32h7xx_hal_adc.c` lines 3713 to 3716, an erratum workaround in ST's own
words: if `ADEN` is set less than four ADC clock cycles after the `ADCAL` bit,
continue setting `ADEN` until `ADRDY` becomes 1.

So one write followed by a poll is **not enough.** The enable has to be
re-asserted inside the wait, and the loop writes `ADEN` on every pass before
looking at `ADRDY`, printing how many times it did. A converter that starts most
of the time is worse than one that refuses, because the times it does not start
look like a wiring fault somewhere else entirely.

Calibration is **offset only and single ended**, and both are choices rather than
defaults that happened. `ADCALDIF` at bit 30 selects differential and is left
clear. `ADCALLIN` at bit 16 adds linearity alongside offset and is deliberately
not done: offset is what a first conversion needs, and adding linearity now would
mean two things were new at once if the result disappoints. It is one bit when
wanted, and the file says so at the bit.

### What stands between this and a sample

One datasheet answer, and one line of a header.

1. **The channel, its sampling time, and the resolution.** Which converter input
   reaches which pin on this package is a datasheet question, and
   `docs/the-board-and-the-wiring.md` records that datasheet as unread. Steps 6,
   7 and 8 all wait on it. Guessing a channel would configure the converter to
   sample something, and a plausible reading from the wrong input is the exact
   failure this project exists to make impossible.
2. **The rising-edge value in `EXTEN`.** `EXTSEL` is 13 for TIM6 TRGO from
   `stm32h7xx_ll_adc.h` line 993, so that half is in hand; the edge is one more
   line of the same header.

And then a flash, which waits for the board to be back on the bench.

## Vendor code

The device header and the startup file come from the vendor pack and are **not
vendored into this repository**, because that pack's licence differs file by
file and a project that pasted it in would be redistributing terms it had not
read. Point CMake at wherever it was unpacked:

    cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
      -DCMSIS_DEVICE_DIR=<path> -DSTARTUP_FILE=<path>

No generated code appears here as though it had been reviewed.
