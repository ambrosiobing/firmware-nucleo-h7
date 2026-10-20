# Sampling on a timer at exactly 1 kHz

Status: c=board cpp=host python=host rust=host

Chapter 6 of the NUCLEO-H7A3ZI-Q firmware volume. Three ways to sample at
1 kHz, one application, and an external witness that decides which of them
actually does.

**Four implementations of the witness, proven equivalent on Sunday
4 October 2026**, which is the half of this project that can be proven. All four
agree on all nineteen fields of all nine captures, in WSL on the win11 skyhorizon
demo laptop, bing@JPTOUPM678, with cargo 1.99.0, gcc 15 and Python 3.14.4.

**Where the three back ends stand, Monday 19 October 2026.** The timer back end
runs on the board and has been measured through the witness. The tick back end
runs and has been measured the same way. The transfer-engine back end is now
WRITTEN, every register from ST's material for this part, and **has never been
compiled**: continuous integration does not build P06's targets because they
include the vendor device header this repository does not vendor, and the one
laptop that could build them has no toolchain installed as of that date. Each
still refuses at run time rather than guessing a setting it has not read, which
is a design property and not a statement about which of them work; that sentence
used to read "the three back ends still refuse", which was true when written and
had stopped being true for two of them.

The witness is arithmetic
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

**THE CLAIM IS MEASURED AND NOT MET, and that belongs here rather than only at the
end of this page.** Criterion 1 fails by 2804 parts per million on the
specification's 60 second run and by 2818 by an independent instrument. Criteria 2,
3 and 4 pass, two of them at the witness's floor rather than the board's. The
failure is structural: the timer divides the oscillator the part boots on, that
oscillator runs 2630 to 3257 ppm high on this board, and a 1000 ppm criterion cannot
be met from a source wrong by three times it, so **no finer instrument changes this
result.** The verdict with every figure, both instruments and what would change it
is the last section of `MEASUREMENT.md`.

It is not a partial pass and the claim is not restated as approximate to fit the
reading. What the project delivers is the failure together with its cause, measured
from outside the part: the firmware reported no overruns and eight bring-up steps
reading back correctly while running 2818 ppm fast, and nothing on the die noticed.

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
    acq_errors.h       one refusal numbering, shared, and why -1 is reserved
    adc_bringup.h      the converter's eight steps, and the trigger as a parameter
    adc_bringup.c      those steps, shared by all three back ends
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
| the marker pin, which is what any witness actually watches | **sourced, at both of its positions.** PB4 at CN7 pin 19, Arduino D25, from UM2408 Rev 6 Table 18 page 44, and at **CN12 pin 27** from Table 19 page 47. **The Zio position is covered while the X-NUCLEO-53L8A1 is stacked**, confirmed by looking at the board on Tuesday 20 October 2026, so the wire goes to CN12 pin 27. Table 18 is this board's own table and not the H745's beside it |
| the ADC external trigger value naming TIM6 TRGO | **sourced.** `EXTSEL` 13, from `stm32h7xx_ll_adc.h` line 993, cross-checked against TIM1 CH1 carrying no `EXTSEL` bits at all, which the field's table requires at position zero |
| the DMAMUX1 request number for the converter | **sourced.** 9, from `stm32h7xx_hal_dma.h` line 229. This one belongs to `acq_dma_double.c` |
| **the converter bring-up sequence** | **not sourced, and it gates all three back ends.** Deep power-down exit, the voltage regulator and its start-up wait, calibration, channel preselection, boost and clock mode, enable and the ready flag. ST's `stm32h7xx_hal_adc.c` implements all of it, so it is derivable from the pack the way the clock tree was, but it has not been read |
| the rising-edge enable beside `EXTSEL` | not sourced; it is the `EXTEN` field and one more line of the same LL header |

**So four of six are in hand and the fifth is the one that matters.** Three
sourced values cannot be written anywhere useful until the converter exists,
because `EXTSEL` selects a trigger for a peripheral that is not configured and
the timer clock drives a TRGO that nothing consumes. The honest order is the
bring-up first, then the three values, then a measurement.

**That table is a snapshot of Tuesday 6 October 2026 and is kept as one.** All
six rows closed on Wednesday 7 October 2026 and the converter now samples on the
board; see "It samples, Wednesday 7 October 2026" below for what that does and
does not establish. The first two steps of the honest order are done and the
third, the measurement, has not started.

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

## Steps 4 and 5 on the board, Wednesday 7 October 2026

Written, built and then **run**, all on the same day, so the capture in the
section above predates them and this one supersedes it. FLASH 11468 to 12740
bytes. Three captures, one of them from a single clean RESET press at 1384 bytes,
all identical where they printed:

    nucleo-h7a3-sampling
      acquisition   timer
      nominal rate  1000 Hz
      instrument    core cycle counter
      tick rate     64000000 Hz
      tim6 clock            64000000 Hz  (pclk1 64000000, /1, timpre 0)
      adc control register       as found 20000000  DEEPPWD 1  ADVREGEN 0
      adc leave deep power-down  ok       wrote 00000000  read back 00000000
      adc regulator enable       ok       wrote 10000000  read back 10000000
      adc regulator start-up     ok       wrote 0000000A  read back 10000000
      adc clock mode sync /4     ok       wrote 00000003  read back 00030000
      adc boost mode             ok       wrote 00000001  read back 10000100
          kernel clock 16000000 Hz from HCLK 64000000 over 4, boost 1
      adc calibration            ok       wrote 80000000  read back 10000100
          calibration took 180 polls
      adc enable until ready     ok       wrote 00000001  read back 00001001
          the enable was re-asserted 4 times, which the erratum requires
      adc preselection           REFUSED  the channel is unsourced
    acq_start failed: -11

**Every line was predicted before the flash**, including the as-found value, the
boost arithmetic and the refusal code.

### The as-found line settled the question the earlier run could not

`as found 20000000` with `DEEPPWD 1` and `ADVREGEN 0`. So **deep power-down was
genuinely exited**, rather than merely reading clear afterwards.

That is worth stating as a closed loop rather than a detail. The three step run
printed `wrote 00000000 read back 00000000` and the pages had to say only that
the bit read clear, because the step did not print what it found. The as-found
line was added for exactly this, the prediction that `DEEPPWD` would read 1 at
reset was written down before the flash, and the board agreed. **One line of
output turned a claim that could not be made into one that can.**

### Five register facts, each confirmed by a read-back

| Fact | Evidence |
|---|---|
| `DEEPPWD` is bit 29 and **set at reset** | As found `20000000` |
| `ADVREGEN` is bit 28 | Written and read back `10000000` |
| `CKMODE` field value 3 means divide by four, at bit 16 | Wrote 3, read back `00030000` |
| `BOOST` is two bits at bit 8 | Boost 1 gives `CR` = `10000100`, the regulator bit still set beside it |
| `ADCAL` is bit 31 and self clearing | Wrote `80000000`, and `CR` reads `10000100` with no `ADCAL`. **The count varies: 180 on Wednesday 7 October 2026 and 187 on Thursday 8 October 2026**, so the self clearing is the fact and the duration is an observation |

The kernel clock arithmetic also came out exactly as `adcmath` computed it on a
laptop that compiles nothing for this part: 16 MHz from HCLK 64 MHz over 4, and
boost 1 from the halved 8 MHz sitting between the 6.25 and 12.5 MHz thresholds.

**One bit was unexplained and was named rather than guessed, and on Thursday
8 October 2026 it was read.** The ready report prints `ISR` and it read
`00001001`. Bit 0 is `ADRDY`, the flag the loop waits on.

**That bit is `LDORDY`, and closing it exposed a wrong claim in this
repository**, which is why it was worth the one grep it was always said to need.

`ADC_ISR_LDORDY`, bit 12, mask `0x00001000`, described in the device header for
this exact part as the "ADC LDO output voltage ready bit". So the `00001001`
read-back was always `ADRDY` at bit 0 plus the **analogue regulator's own ready
flag**, and this file had been printing that flag for a day without recognising
it.

**And `acq_timer.c` said twice that this flag does not exist.** Its words were
"there is no ready flag for this one, so there is nothing to poll", used to
justify the ten microsecond regulator wait being a blind delay. There is a flag.
ST references it six times in its own driver headers, `stm32h7xx_hal_adc.h` at
913 and 1376 and `stm32h7xx_ll_adc.h` at 655, 7546, 7550 and 7552, and uses it
nowhere in the `.c` files that implement the start-up.

**The error was inferring the absence of a flag from ST's choice not to poll
one**, which is the same shape as taking a value off a schematic and calling it a
meaning. Reference code shows one working sequence; the register list shows what
the part can do. Only the second question was ever the right one to ask, and the
answer had been sitting in the header the whole time.

**The delay stays and the flag is reported beside it**, not gated on. ST's
documented start-up time is the sufficient condition and it is met; the flag has
been observed once. Gating the whole bring-up on a bit learned about today would
stake it on a single observation. If it reads set on every run for a while,
promoting it to a bound poll is a two line change and the delay becomes the
fallback, which is the shape the `ADEN` erratum settled into on Wednesday
7 October 2026.

**What to watch for in the new line.** `set` is expected, because the documented
start-up time has just elapsed. **`CLEAR` would be the interesting result**, and
it would mean either that the delay is not sufficient on this part or that the bit
does not mean what the header says. Either deserves a flash of its own.

### The erratum does not bite here, settled by one flash

A number was read as a mechanism, challenged, and then settled by an experiment
that could have gone either way. This is the sequence rather than the conclusion,
because the sequence is the transferable part.

**What the first run said.** `the enable was re-asserted 4 times`. ST's workaround
at `stm32h7xx_hal_adc.c` lines 3713 to 3716 says that if `ADEN` is set less than
four ADC clock cycles after `ADCAL`, the enable must be re-asserted until `ADRDY`
becomes 1. Four passes looked like that happening.

**Why that showed nothing.** The loop writes `ADEN` and then polls `ADRDY` on
every pass, so four passes is equally consistent with two stories: the first
three writes were ignored and the fourth took, or the first write took and the
flag needed four passes to rise. Nothing printed separated them, so the number
was recorded and the mechanism was declined.

**The prediction, written before the flash.** The condition is *less than four
ADC clock cycles* after `ADCAL`, and two `printf` calls sit between the
calibration step and the enable. About fifty bytes at 115200 baud is some four
milliseconds, which at this image's 16 MHz ADC kernel clock is roughly sixty four
thousand ADC cycles. So the window closes many thousands of times over before
`ADEN` is written, and the condition cannot be met by this code at all. The
single write should therefore succeed.

**What the board said**, three captures including one from a single clean RESET
press at 1396 bytes, all identical:

    adc enable, one write      ok       wrote 00000001  read back 00001001
        ready after 4 polls with NO re-assertion, so the erratum did not bite here

**AND THE "SAME COUNT" HALF OF THAT ARGUMENT IS WITHDRAWN, Friday 9 October 2026,
while the conclusion stands on better footing than it had.** A fourth run of the
same image reported `ready after 5 polls`, so the count varies: 4, 4, 4 and then 5.

Two things were wrong with leaning on the equality. The count is not stable, so 4
matching 4 was a coincidence inside a quantity that moves. And **the two numbers
were never the same unit of work**: a pass of the re-asserting loop writes `ADEN`
and then tests `ADRDY`, where a poll of the single-write path only tests it. Four
passes and four polls describe different amounts of work and should not have been
compared as though they described the same.

**What the observation actually establishes is stronger and does not need the
equality.** If re-assertion were necessary, the single write would never have
become ready at all: `ADRDY` would stay clear and the step would exhaust
`ADC_READY_POLLS_MAX`, which is 1 000 000. It became ready in four polls, then
four again, then four, then five. **Ready at all, in a handful of polls against a
bound of a million, is the finding.** The arithmetic leg is untouched by any of
this: two `printf` calls sit between `ADCAL` and `ADEN`, about four milliseconds
at 115200 baud, which is some sixty four thousand cycles of the 16 MHz ADC clock
against a condition of four, so the window is missed by about four orders of
magnitude.

### What that claim is, and is not

It is **not** that ST is wrong or that the erratum does not apply to this part.
What is established is narrower and more useful: **this image does not meet the
condition**, so the workaround is unnecessary here. The rule stands for any
sequence that does meet it, which means any code putting `ADEN` within four ADC
clock cycles of `ADCAL`.

**And that is exactly why the loop is kept.** It runs only when the single write
was not enough, so in this image it costs nothing, and it covers the case a later
version reintroduces by dropping the console output or by moving the enable next
to the calibration. `acq_timer.c` says so where the loop sits, so the protection
is not removed as dead code by somebody who measures only this build.

One honest footnote on the timing. Four polls of a read, compare and increment at
64 MHz is a few tens of nanoseconds, which is under one period of the 16 MHz ADC
clock, so the flag was effectively up by the first look. The per-poll cost has
not been measured here and no figure is claimed from it.

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

## It samples, Wednesday 7 October 2026

After the vector table gained its ADC entry the same image was rebuilt, flashed
and run on the win11 skyhorizon demo laptop. FLASH 14 192 bytes, `.isr_vector`
224. The eight steps repeated identically, and then this, which this project had
never produced:

    tim6 started, so conversions begin on its update event
    seq 15  blocks 15  mean 4  overruns 0
    seq 30  blocks 30  mean 4  overruns 0
    seq 45  blocks 45  mean 4  overruns 0
    ...
    seq 375  blocks 375  mean 4  overruns 0
    seq 390  blocks 390  mean 3  overruns 0

Twenty-six report lines over roughly twenty-five seconds of console, 390 blocks
of 64 conversions, 24 960 conversions in all, and no fault.

**The chain that had never run end to end now does**: TIM6's update event, TRGO,
the converter starting on a hardware edge with the processor not in the path, end
of conversion, the interrupt controller, the vector, the handler, the block, and
`main` draining it. Earlier the same day all eight steps reported ok while none of
that was true, which is why the distance between a configuration and a result is
the thing this project keeps relearning.

### Where the double entry pays

`seq` equals `blocks` on every one of the twenty-six lines and `overruns` is 0 on
every one. Those are **three counters that do not read each other**: `ready_seq`
increments in the handler, `blocks` increments in `main`, and `overruns`
increments in the handler when a block becomes ready while the previous one is
still uncollected. A single lost block moves all three at once, `seq` above
`blocks` and `overruns` off zero. None of them moved.

That is the strongest thing in the capture, and it is a statement about **not
losing samples** rather than about when any of them was taken.

### Half of the published prediction could not have failed

Before the flash this README's own words were that `seq N blocks M mean X
overruns 0` would appear "about once per second with `blocks` rising by roughly
15 each time". The second half of that was not a prediction about the board at
all. `main.c:84` reports when `blocks - last_report >= ACQ_RATE_HZ / b.count`,
and that is 1000 divided by 64, which is 15. **The increment was arithmetic in the
source and could not have come out otherwise**, whatever the hardware did, and it
was published as though the hardware would decide it.

This volume's own rule is that an experiment must be able to fail, and the same
requirement applies to a prediction. Half of this one was exempt. The half that
could fail, and did not, was that the line appears at all and that `overruns`
stays at zero.

**And the comment above that line is what misled the prediction**, so it is
corrected in the same commit. It read that the cadence was computed from the
nominal rate "so the reporting cadence does not itself depend on the thing being
measured". The threshold does not; the cadence in time depends on the actual rate
entirely, because a report is emitted every 960 samples whenever those samples
arrive. The intent was that no software delay sits in the path, which is true and
worth keeping, and the sentence said something stronger that is false.

### The rate is still not measured, and the board cannot measure it

What the capture says about rate is only the **wall-clock cadence of the lines**,
and the clock that cadence was read against was a PowerShell loop printing a
marker every ten iterations of a 500 millisecond sleep. Between the `[10s]` and
the `[30s]` marker `seq` went from 60 to 390: 330 blocks, 21 120 conversions, in
a nominal 20 seconds, which is 1056 Hz.

**That figure is an upper bound rather than an estimate.** Each loop iteration
costs its sleep plus its own work, so the elapsed time was longer than 20 seconds
and the true rate is below 1056 Hz. The direction of the bias is known and its
size is not, which is a fair definition of not an instrument.

So 1056 Hz is **consistent with 1000 Hz and is not evidence for it**, and none of
`MEASUREMENT.md`'s four criteria is touched by it: not the mean within 0.1
percent, not the interval spread, not the worst case interval, not dropped or
doubled edges. All four are about the instants at which conversions happened, and
nothing in this capture observed an instant.

**The board cannot settle it in principle and not merely in practice.** Any
timebase on this die that could count the sampling interval is derived from the
same oscillator that defines it, so firmware timing itself would confirm a wrong
rate exactly as readily as a right one. That is why the project was built around
an external witness before any of it was written. The MCC 118 on the Raspberry Pi
watching the marker on PB4 is the measurement, at whichever of its two positions
the shield leaves reachable, which `docs/the-board-and-the-wiring.md` 4.4 states.
The wire is not
connected, and until it is, the 1 kHz in this project's title is a claim.

### The converted value, which is not the result

`mean` reads 3, 4 or 5 out of 65 535, which is what a floating PC3 reads, and
that was stated in advance as meaningless. One thing in it earns a sentence: it
**varies between reports**. A data register that was never written, or a converter
returning a constant, would give the same number on every line. A small varying
number is weak positive evidence that the conversions are real conversions. Weak,
because this cannot tell a floating input from a grounded one, and because this
project's subject is the rate.

### What this section used to say stood in the way

Both items are gone, and a third that was on no list is what actually stopped it.

| item | how it closed |
|---|---|
| the channel, its sampling time and the resolution, all waiting on an unread datasheet | **settled from ST's pack.** Channel 13 on PC3, stated twice in `ADC_DualModeInterleaved`, with `stm32h7a3zi.pdf` still unread |
| the rising edge value in `EXTEN` | **read.** 1, from `LL_ADC_REG_TRIG_EXT_RISING` being `EXTEN_0` alone |
| not listed: `EOCIE` in the converter's own `IER` | found by reading the file once it stopped refusing |
| not listed: `ADSTART` | the same reading |
| not listed: **a vector for the handler to reach** | found by flashing. Three things must be true for a handler to run and only two of them read back |

## The overrun flag on the board, and a correction, Thursday 8 October 2026

`p06-sampling-timer` was rebuilt, flashed and run with `OVRIE` enabled. FLASH
14 192 to 14 732 bytes. All three back ends were built, not only this one, because
`acq.h` gained a function and `main.c` calls it, so a back end missing it would
have failed to link. **The two shared builds grew by exactly 476 bytes each**,
`systick` 9460 to 9936 and `dma` 9476 to 9952, which is the kind of agreement that
would not hold if back-end-specific code had reached a shared file.

The capture ran across a reset, so it holds two runs of the same image.

### Two register facts, both read back

| Fact | Evidence |
|---|---|
| `OVRIE` is bit 4 of `IER`, and `EOCIE` is bit 2 | Wrote `00000010` for `OVRIE` alone, read back `00000014`, which is both |
| `OVRMOD` reads **0 as found** | Printed rather than written. An overrun therefore **preserves** the data register and discards the new conversion |

`OVRMOD` is the bit that decides which samples a gap contains, so the count is not
interpretable without it. It is printed and not chosen, because neither value is
obviously right here and choosing one would be a configuration decision presented
as a fact.

### The zero is now one somebody earned

`convovr 0` on every line of both runs: **3135 blocks, 200 640 conversions**, with
`overruns` also 0 throughout. That is the longest this project has run, by a factor
of eight over Wednesday 7 October 2026's 390 blocks, and unlike that night's
`overruns 0` this pair of zeros could have come out otherwise.

The arithmetic says they should be zero, which is why a non-zero reading would have
been the interesting outcome: at 1 kHz a conversion takes roughly 51 microseconds
inside a 1000 microsecond period, so the handler has about twenty times the slack
it needs.

### A correction to what this README said last night about the rate

**It said 1056 Hz was an upper bound and the true rate was below it**, reasoning
that each loop iteration costs its sleep plus its own work so the elapsed time was
longer than the nominal 20 seconds. That is a one-directional error and it is not
the dominant one. This capture refutes the claim with the same image in a single
run:

| window | blocks | implied rate | report lines |
|---|---|---|---|
| pre-reset, `[5s]` to `[15s]` | 165 | 1056.0 Hz | 11 |
| post-reset, `[20s]` to `[30s]` | 150 | 960.0 Hz | 10 |

A true 1000.0 Hz emits one report line every **0.9600 s**, so **10.4167 lines per
10 s window**. Every such window must therefore show 10 or 11 lines, which is 150
or 165 blocks, which is 960.0 or 1056.0 Hz **and nothing in between**. Both
observed values are exactly those two outcomes.

So the error is **two-directional quantisation** rather than one-directional
overhead, and "upper bound" was the wrong shape of claim. What the two readings do
establish is weaker and cleaner than what was claimed: they are consistent with
1000 Hz, they bracket it, and the method's resolution at this window length is
about ten percent.

### And the correction opens a route this README had written off

The previous section said the board cannot measure its own rate, which is true and
remains true: every timebase on the die descends from the oscillator that defines
the interval. **It then treated the console path as no instrument at all, and that
was too strong.** The PC clock is an *independent* timebase. The console path is a
coarse external witness, and its resolution improves with the window:

| window | report lines | granularity |
|---|---|---|
| 10 s | 10.4 | ±9.6 percent |
| 60 s | 62.5 | ±1.6 percent |
| 600 s | 625 | ±0.16 percent |
| 960 s | 1000 | ±0.10 percent |

Stamping each line's arrival removes the quantisation rather than shrinking it,
because the window edges become observed events instead of guesses, and the serial
latency is roughly constant so it cancels in a difference.

### The method and the threshold for that attempt, published before it is run

Stated here before the capture exists, so the answer cannot be read to fit.

**Method.** Read COM13 at 115200 on win11 skyhorizon, stamping each received
report line with `Get-Date`. Take the first and last stamped lines, divide the
difference in `blocks` by the difference in time, multiply by 64 samples per block.
Run for at least 600 seconds after a single RESET press.

**Threshold.** Criterion 1 of `MEASUREMENT.md` is the mean rate within 0.1 percent
of 1000.0 Hz, so **999.0 to 1001.0 Hz passes and anything outside it fails.**

**What would refute the method rather than the firmware.** If two consecutive
600 second runs disagree by more than 0.05 percent, the instrument is not stable
enough to make the claim and the number is withdrawn rather than averaged.

**What this can and cannot settle, stated flatly.** It can settle criterion 1 and
nothing else. Criteria 2, 3 and 4 are the interval spread, the worst case interval
and dropped or doubled edges, and all three are about the instants of individual
conversions. The console sees only block completions, 64 samples apart, so it
cannot observe an interval at all. **The MCC 118 on PB4 remains the
only route to three of the four**, and those three are the chapter's actual
subject.

### The converted value changed by three orders of magnitude, and it was the floating pin

**Closed by the grounded control below, Thursday 8 October 2026.** The account that follows is left as it was written, because it is the reasoning that led to the control and one hypothesis it eliminates is eliminated on evidence that is still worth reading.

`mean` read **3 to 5** on Wednesday 7 October 2026 and **3037 to 3397** on
Thursday 8 October 2026, with nothing deliberately connected to PC3 on either
night. That is 0.2 mV against about 162 mV at a 3.3 V reference, and the spread
within the second run is 360 counts, about 11 percent of its own mid value.

**One hypothesis is already eliminated by data in hand, which is the only reason it
is worth writing down.** A frozen data register would explain the first reading: if
`OVR` had been set with `OVRMOD` clear, `DR` would hold a stale value, and
the Wednesday 7 October 2026 reading's suspiciously steady 3 to 5 across 26
reports looks like exactly that.
It cannot be right. `blocks` incremented at the nominal rate all night, which
requires `EOC` once per conversion, which requires `DR` to have been written each
time. So both readings were live readings of a floating pin and the converter is
not implicated.

**What changed is therefore the pin's environment, and this repository does not know
what.** A hand nearby, a different surface, a different USB port and anything
resting against the Arduino headers would all do it. One observation is not a
mechanism and two observations with an unrecorded difference between them are not
either.

**The control, with its threshold published first.** Tie PC3 at CN9 pin 5 to any
pin marked GND on the Arduino headers, where the silkscreen is authority enough for
ground, and capture again. **`mean` should read within a few counts of 0 and stay
there.** If it still reads near 3200 with a wire to ground then the floating input
is not the cause and something more interesting is happening.

**None of this bears on the project's claim**, which is about the rate. It is
recorded because an unexplained three orders of magnitude is worth a line whether
or not it matters, and because the eliminated hypothesis cost nothing to eliminate.

## Criterion 1 has an answer and it is a failure, Thursday 8 October 2026

**1002.818 Hz, which is +2818 parts per million against a threshold of plus or
minus 1000.** Criterion 1 fails by a factor of 2.8. The threshold was published
before the run and is not being revisited now that the answer is in.

This is the first measured statement about the rate in this project's history, and
it is worth more as a failure than it would have been as a pass, for the reason in
the last subsection.

### Three runs were invalid first, and the method as published would have taken them

The method above fixed a threshold for the answer and no check that the instrument
was working. That omission was the real defect and it is the same shape as the
vacuous half of the prediction recorded earlier on this page.

| run | lines | span the lines require | span observed | rate it reported |
|---|---|---|---|---|
| 1 | 743 | 11 145 | 9465 | 946.782 Hz |
| 2 | 706 | 10 590 | 90 | 9.011 Hz |
| 3 | 772 | 11 580 | 13 005 | 1301.394 Hz |

A report line is emitted every 15 blocks, so **the line count and the block span
are two routes to one number and must agree.** In all three they did not, by
factors from 1.18 to 118, and that was visible in the data before any rate was
computed.

**One mechanism explains all three.** Each run's first row sat exactly 15 blocks
past the previous run's last row, 9780 then 9795 and 9885 then 9900. That is a line
the serial driver buffered while the port was closed, delivered on reopening and
stamped with a **fresh arrival time against a stale block count**. Any span
starting there is wrong, in whichever direction the backlog happens to fall. Runs 1
and 2 carried a restart as well, because the instruction at the time was to press
RESET after starting the reader, which put a discontinuity a few seconds into every
capture.

### The run that passed its own check

    rows 813  banners seen 0  steps not equal to 15: 1
    longest clean segment rows 82 to 812, blocks 24450 to 35400, 698.831 s
    mean rate 1,002.818 Hz   deviation 0.2818 percent   FAIL

**The segment is internally exact**, and that is what makes the number believable
rather than merely the latest. 730 steps at 15 blocks each requires a span of
10 950 and the observed span is 10 950, a difference of zero. 700 800 conversions
in one unbroken stretch. `banners seen 0`, so no restart. The single bad step is
where the 82 line backlog meets the live stream, and the clean-segment rule threw
the backlog away, which is the job it was added for.

### The cause is the oscillator, and a second instrument had already said so

TIM6 runs from the APB1 timer clock, which the firmware's own banner reports as
64 000 000 Hz with the prescaler at one and `TIMPRE` clear, so the divisors were
computed for exactly 64 MHz and the sampling rate carries that oscillator's error
directly.

`docs/the-board-and-the-wiring.md`, in the section "The internal oscillator,
nominal 64 MHz", records it as **always above nominal, 2630 to 3257 parts per
million high**, measured by the on-board gated counter against the real-time clock
crystal's sub-second register.

| instrument | timebase it trusts | result |
|---|---|---|
| the on-board gated counter | the real-time clock crystal | oscillator 2630 to 3257 ppm high |
| this console script | the host PC's clock | sampling rate +2818 ppm high |

**Neither instrument is the oscillator under test, and they agree.** The band
requires a rate between 1002.630 and 1003.257 Hz and the measurement is 1002.818
Hz. The sign was predicted too, by that document's opposite-sign table: at the
reset clock a requested delay comes out 3027 ppm short, so a requested 1000
microsecond period is short and the rate is high. It is high.

A host clock cannot account for 2818 ppm. An unsynchronised PC crystal is tens of
parts per million and a synchronised one far less.

### So the claim in this project's title is not reachable on this clock

**Not by a little.** The internal oscillator misses criterion 1 by about a factor
of 2.8 on its own, before any question of jitter, and no amount of firmware care
changes that. Four options, not equivalent, and the first is withdrawn:

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

### Why a failure here is worth more than a pass would have been

A pass would have been one number agreeing with one expectation. **This failure
arrived with its cause already measured by an independent instrument, its sign
already predicted in writing, and its magnitude inside a band recorded days
earlier.** The chapter's thesis is that a plausible wrong rate is indistinguishable
from a correct one from inside the part, and the part reported `overruns 0`,
`convovr 0` and a flawless eight step bring-up while running 2818 parts per million
fast. Nothing on the die noticed. That is the thesis, measured.

## The LDORDY flash, and two things it showed that were not predicted

Built, flashed and captured as consecutive steps on Thursday 8 October 2026, which
is now the rule rather than a habit: the board is shared with another project, so a
capture taken without reflashing first is a capture of an unknown image. FLASH
14 732 to 14 848 bytes.

### The prediction held exactly, and it could have failed two ways

Published before the flash, then observed:

    adc regulator ready flag   set    isr 00001000  (LDORDY, bit 12)

**`set`, and `ISR` reading `00001000` rather than anything else.** Both halves were
stated in advance. The flag could have read `CLEAR`, which would have meant either
that ST's documented ten microsecond start-up is not sufficient on this part or
that the bit does not mean what the header says. And `ISR` could have carried other
bits; `00001000` is `LDORDY` alone, with `ADRDY` correctly absent because the
converter is not enabled until two steps later.

So the regulator's own ready flag is confirmed on hardware, reading set at exactly
the point ST's delay says it should. The delay still runs and the flag is still not
gated on, for the reason at `ADC_VREG_STARTUP_US`: it has now been observed twice
rather than once, which is not yet a licence to stake a bring-up on it.

### Calibration took 187 polls, where this page said 180 as though it were a property

It is 180 on the runs of Wednesday 7 October 2026 and 187 on Thursday 8 October
2026. **The self clearing is the fact; the duration is an observation**, and three
places in this repository had written the duration as if it were the former. All
three now say so.

Nothing depends on the count, which is exactly why it was easy to write carelessly.
The bound it is checked against is far above either figure, so neither run came
close to the refusal path.

### The converted value oscillates regularly, and it was pickup on the floating pin

**Closed by the grounded control below, Thursday 8 October 2026**, which removed it entirely. Left as written, because it is what put the control on the critical path.

Sixty six reports, `mean` from **2649 to 3116**, and it is not noise. Local maxima
fall at reports 5, 13, 23, 32, 40, 49 and 58, so the gaps are **8, 10, 9, 8, 9 and
9**. One report is 15 blocks of 64 samples, so at the measured 1002.818 Hz the cycle
is about **8.5 seconds, near 0.118 Hz**.

**The regularity is the finding, and it is what the earlier account did not
anticipate.** Thursday 8 October 2026's earlier note put the change in `mean` down
to "the pin's environment", which covers ambient pickup wandering. **Ambient wander
does not produce six consecutive intervals of 8 to 10 reports.** Something periodic
is present.

**And the earlier capture cannot be used to extend this backwards.** It was
19 reports with peak gaps of 3 and 6, which is too short to establish any period, so
whether the same oscillation was present then is unknown rather than likely. A
longer capture is the only way to say.

**No mechanism is offered.** Naive alias arithmetic does not single one out, because
the signal passes through two stages of averaging and decimation before it reaches
the console: a 64 sample block mean, then one report in every fifteen blocks. A
frequency guessed from the visible 0.118 Hz could correspond to many inputs.

**The discriminator is the control already specified above**, whose threshold was
published before any of this: tie PC3 at CN9 pin 5 to a pin marked GND and capture
again. Pickup on a floating input vanishes when the pin is held at ground. A
periodic reading that survives grounding is a different and more interesting
problem, and in either case the answer arrives for the cost of one jumper wire.

**None of this bears on the project's claim**, which is about the rate. It is
recorded because a regular 0.118 Hz oscillation on an input nobody connected is the
kind of thing that becomes obvious in hindsight and expensive if it is first
noticed in a measurement that matters.

## The grounded control, and both open observations close at once

Thursday 8 October 2026. One jumper wire, and the threshold for it had been
published before the wire went on: `mean` within a few counts of 0 and steady.

    seq 15  blocks 15  mean 1  overruns 0  convovr 0
    seq 30  blocks 30  mean 2  overruns 0  convovr 0
    ...
    seq 450  blocks 450  mean 1  overruns 0  convovr 0

**`mean` reads 1 or 2 across thirty reports, a spread of one count. The control
passes.**

| | floating | grounded |
|---|---|---|
| `mean`, average of the reports | 2861.7 counts | **1.37 counts** |
| the same in millivolts at a 3.3 V reference | 144.10 mV | **0.07 mV** |
| spread across the capture | 467 counts | **1 count** |
| the regular oscillation | maxima every 8 to 10 reports | **absent** |

The offset fell by a factor of about **2094** and the spread by a factor of
**467**. Same image, same pin, same converter configuration, one wire added.

### What this settles

**The offset was the floating input.** Not the converter, not the configuration,
not the data register.

**And the regular 0.118 Hz oscillation was pickup on that floating input.** It was
reported one commit earlier as new and unexplained, with the explicit note that
ambient wander does not produce six consecutive intervals of 8 to 10 reports and
that something periodic was present. Something periodic was present, it was reaching
the part through an unconnected pin, and holding that pin at ground removed it
completely. **There is no longer any range for a period to live in:** the whole
capture spans one count.

**The converter's own zero is about 1.5 counts, which is 0.076 millivolts.** The 1
and 2 alternation is integer division of a 64 sample sum whose true value sits
between them. That is a result in its own right and it was not the one being looked
for: **it says the offset calibration worked.** A converter with a real offset error
would read a steady number of counts above zero on a grounded input, and this one
reads the bottom of its range.

### And it reconciles the two nights, which guesswork had not

Wednesday 7 October 2026 read **3 to 5**. Thursday 8 October 2026 read **2649 to
3116**. The grounded zero is **1 to 2**.

So the Wednesday 7 October 2026 reading sat **near the converter's true zero** and
the Thursday 8 October 2026 one sat far from it. The pin was close to ground
potential on the first night and something coupled into it on the second. The
earlier account called that "the pin's environment" and could not do better than the
phrase; the control turns it into a measurement, because it establishes where zero
actually is and therefore which of the two readings was the anomalous one. **It was
the Thursday 8 October 2026 one.**

### What it does not settle, stated so nobody looks for an answer that is not here

**What was coupling in.** The control answers "was the floating pin the cause", and
the answer is yes. It does not identify the source, and nothing here will: a
grounded pin carries no information about what a floating one was picking up.

That question is also no longer worth asking for this project. PC3 will carry a real
source when the value matters, and a real source has an impedance low enough that
this coupling becomes irrelevant. **The sampling time was chosen for exactly that
uncertainty** and is recorded above as a choice: 810.5 cycles is the tolerant option
precisely because what would be connected to PC3 was not decided.

### A note on how the wire went on, because the method is better than the instruction

The instruction said PC3 at **CN9 pin 5**. The wire went on at PC3 on **CN11**
instead, because the Morpho connector carries signal-name silkscreen and CN9 does
not.

**That is the better of the two and the reason generalises.** ST's own example
readme, the same authority that settled the channel, gives the pin as "ADC_CHANNEL_13
on pin PC.03 (Arduino connector CN9 pin 5, Morpho connector CN11 pin 37)", so the
two are one net. Wiring at CN9 means **counting pins** on an unlabelled header, and
an off-by-one there lands on a different net silently and reads plausibly, which is
the failure mode this entire volume is organised around. Wiring at CN11 means reading
a **label**, and the label is a better authority than a position.

**So the standing instruction changes:** where a pin exists on both connectors, wire
it at the labelled one and name both in the instruction. The pin number on the
labelled connector does not even need to be right, because the silkscreen is what is
being trusted.

## The error-numbering flash, Friday 9 October 2026

Built, flashed and captured as consecutive steps. FLASH 14 848 to 14 920 bytes.

### All three builds grew by exactly 72 bytes, which is the check that matters

| target | FLASH before | after | delta |
|---|---|---|---|
| `p06-sampling-systick` | 9936 B | 10 008 B | **+72** |
| `p06-sampling-timer` | 14 848 B | 14 920 B | **+72** |
| `p06-sampling-dma` | 9952 B | 10 024 B | **+72** |

The only size-affecting change is `main.c`'s failure message, and `main.c` is
shared, so an **identical** delta is what a shared-only change must produce. A
difference between the three would have meant something back-end specific had crept
in. The 72 splits as 64 bytes of `.rodata` for the three replacement strings and 8
bytes of `.text` for the third `printf` call where there were two. **The named
constants cost nothing**, which is the point: a `#define` that compiles to the same
immediate is free.

### The published criterion was byte-identical output, and 18 of 19 lines met it

All **eighteen configuration read-backs** are identical, line for line: the as-found
control register, every `wrote` and `read back` pair through all eight steps, the
`LDORDY` line, the `OVRMOD` line. That is what this change could have broken and it
did not.

**One line differs**, and it is a measured count rather than a configuration value:

    was: ready after 4 polls with NO re-assertion, so the erratum did not bite here
    now: ready after 5 polls with NO re-assertion, so the erratum did not bite here

### And that makes the criterion itself the thing at fault

**The criterion as published could fail for a reason unrelated to what it was
testing**, which is a defect of the opposite kind to the ones this volume has been
collecting. The others could not fail; this one could fail wrongly.

It demanded byte-identical output across the whole step block, which lumps together
two different kinds of line. A **configuration read-back** must not move: if it does,
the change was not pure. A **measured count** may move freely, because it is a
property of the run rather than of the image. The step block holds eighteen of the
first kind and two of the second, the calibration polls and the enable polls, and
both of those were already known to vary.

**The criterion should have excluded them by name and it did not.** Stated properly
it is: every `wrote` and `read back` pair identical, every refusal absent, and the
two poll counts free to move. On that criterion the change passes cleanly.

### The two poll counts, now both known to vary

| count | observations |
|---|---|
| calibration, `ADCAL` self clearing | 180, then 187, 187, 187 |
| enable, `ADRDY` after one write | 4, 4, 4, then **5** |

Neither has a bound anywhere near it: calibration is checked against 1 000 000 polls
and so is the ready flag. Both were first written as though the figure were a
property of the part. Neither is.

**The enable count moving is what withdrew the "same count" argument above**, and
that withdrawal left the erratum conclusion stronger rather than weaker, which is
worth the second read.

### And `mean` says the ground wire is no longer connected

It reads 2940 to 3411 across twenty four reports, where the grounded control gave 1
to 2. **Nothing in this change touches the analogue path**, and the eighteen
identical configuration read-backs prove the converter is configured exactly as it
was for the control: same channel, same preselection, same sampling time, same
resolution, same trigger.

So the difference is physical rather than firmware, which is what the control's own
published wording anticipated: a different `mean` would mean the wire moved rather
than that the firmware did. The wire is off, or was off for this run.

**That does not disturb the control's result.** The control was a separate capture
with its own threshold, published in advance and met at the time. This capture
simply has PC3 floating again, and a floating PC3 reading about 3200 counts is the
behaviour already recorded for Thursday 8 October 2026.

## The move, done Friday 9 October 2026, and the blind spot it uncovered

`acq_timer.c` went from **889 lines to 346**. `adc_bringup.c` is 563 and
`adc_bringup.h` is 76. One `SOURCES` entry serves all three targets, as scoped.

**What stays in `acq_timer.c` is what is specific to this mechanism**: the ring
buffer, the end-of-conversion handler, the APB1 divisor that feeds the timer clock,
TIM6, and the order in which the converter is armed before the timer starts.

**Two things changed in the moved code and no third.** The trigger became three
parameters, `extsel`, `exten` and a `trigger_desc` printed in words. And `report()`
became `adc_report()` and non-static, because the steps `acq_start` performs after
the bring-up returns must print in the same shape as the eight before them; a bare
`report()` in the global namespace of a project this size is a collision waiting to
happen.

### The criterion was checked statically before the board was touched

**33 console-affecting strings before the move, 33 after**, compared by extracting
every `printf` format and every step name from the old single file and from the new
pair. Exactly one differs:

    lost:    started by TIM6 TRGO on the rising edge
    gained:  started by %s

That is the trigger description becoming an argument, and the call site passes the
same words, so the line is identical on the console. **Everything else, every step
name and every format, survived unchanged.** The flash can still refute this; what
it can no longer do is refute it for a reason that was findable on a laptop.

### And the move uncovered a third blind spot in `check_link_closure.py`

This is the part worth reading, because the check had been reporting success over
work it was not doing.

**Every pattern in that check which spans a multi-line list ends at the first
`INCLUDES`, `VENDOR_INCLUDES`, `DEFINES` or `)`.** A CMake comment can contain any
of them. P06's `SOURCES` list carries a comment holding the words **"a set()"**, so
the parse stopped at that parenthesis, and the three entries below it were invisible:

    c/instr/adcmath.c      never seen
    c/instr/pwmmath.c      never seen
    ${ACQ}                 never seen, so NO back end was ever followed for P06

**So from Wednesday 7 October 2026 the check had not followed any of P06's three
back ends**, while reporting five targets closed and nothing wrong.

**The comment that broke it is the one describing the first two blind spots.** A
note about two defects created a third, in the same block, by containing a
parenthesis.

**And it only surfaced because the new file was added above the truncation point.**
Its includes were followed, the `.c` files satisfying them sat below the cut and
read as missing, so the check reported `NOT CLOSED` on a target that was in fact
fine. Had `adc_bringup.c` been listed below that comment, nothing would have been
reported and the move would have looked clean.

The fix strips CMake comments before any parsing. `test_link_closure_resolves.py`
gains a fourth case pinning it, and the mutation was run: with the stripping made a
no-op, the block stops reaching `${ACQ}` and the test fails. **The suite is now 350
checks, 204 passing and 146 skipping**, and the count guard written on Wednesday
7 October 2026 caught the drift from 349 without being asked.

### The flash, and the criterion is met

    step block: 23 lines, 21 configuration and 2 measured
    configuration lines that differ: 0 of 21

**Every `wrote` and `read back` pair and every step name identical.** The one
differing line is the enable poll count, which the criterion permits, and it moved
5 back to 4, so that count now reads 4, 4, 4, 5, 4 across five runs.

**Two things the board verified that the static check could only argue for.** The
trigger step wrote `0000000D` with `extsel` as a **runtime parameter** rather than a
folded constant, and read back `800005A0` unchanged. And `started by TIM6 TRGO on
the rising edge` is now a `%s` whose argument comes from the call site, and it came
out character for character the same.

### The move is not free, and the size table says where it is paid

| target | FLASH before | after | delta | `.text` | `.rodata` |
|---|---|---|---|---|---|
| `p06-sampling-timer` | 14 920 B | 14 972 B | **+52** | +44 | +8 |
| `p06-sampling-systick` | 10 008 B | 10 008 B | **0** | 0 | 0 |
| `p06-sampling-dma` | 10 024 B | 10 024 B | **0** | 0 | 0 |

**The two unused builds cost exactly nothing**, which is what made adding the shared
file to all three targets safe. They compile `adc_bringup.c` and never call it, so
`--gc-sections` drops it whole. Had either grown, the linker would not be collecting
it and every future back end would carry dead code it does not use.

**And the timer grew 52 bytes, which is the price of sharing rather than a mistake.**
Three causes, all of them consequences of the move itself: the trigger values are
runtime parameters now, so `extsel << POS` is a shift the compiler can no longer
fold; `adc_bring_up` and `adc_report` are no longer static, so neither can be
inlined or specialised inside one translation unit; and the call gains three
arguments.

**That is worth stating rather than glossing.** Sharing a sequence between three
back ends is not costless, and 52 bytes out of 14 972 against three copies of 420
lines is an easy trade, but the trade is real and a reader deciding whether to do
the same thing elsewhere needs the number.

### What is left of the factoring

The second commit, `sampling-systick` calling into the shared bring-up with a
software trigger, which is a **new claim** and needs its own capture. The scope
note above said to keep the two apart so that a failure cannot be ambiguous between
the move and the new back end, and that still holds.

### The stamp on the board, and the criterion it was held to

    image         15196 B of flash, the figure the build reports
    main.c built  Oct  9 2026 09:31:08, which moves only when THIS file recompiles

**The published prediction was `15196` and the build reported `FLASH: 15196 B`.**
Two numbers reached by different routes that had to agree: one from the linker's
memory report, one reconstructed on the part from `_sidata`, `_sflash`, `_sdata` and
`_edata`. A wrong symbol, a sign error or a FLASH region the reconstruction did not
account for would each have shown as a mismatch. None did.

**And the compile stamp was checked rather than trusted, which is the point of
having it.** It reads 09:31:08 where the commit it came from was authored at
08:30:31, an hour earlier. That hour is real: it is the gap my own wrong target
names opened, since the first build attempt was refused by ninja. **The two laptops'
clocks were compared before that was concluded**, aquamarine reading 09:35:04 four
minutes after the compile, so the offset is the gap and not a clock difference. A
stamp is only as good as knowing whose clock it is.

**The step block is unchanged for the third capture running**: 21 of 21
configuration lines identical. The banner is deliberately two lines longer.

### The calibration poll count, and a claim of mine narrowed

| day | readings |
|---|---|
| Wednesday 7 October 2026 | 180 |
| Thursday 8 October 2026 | 187, 187, 187 |
| Friday 9 October 2026 | 181 |

Set {180, 181, 187}, range 7 over five runs.

**Two commits ago this was written as varying between days while holding within a
day. That was too strong and is narrowed here.** The within-day evidence is the
three identical readings of Thursday 8 October 2026 and nothing else; Wednesday
7 October 2026 and Friday 9 October 2026 have one reading each, and a single reading
cannot demonstrate constancy. So the honest
statement is that the count varies over that set, and that on the one day it was
sampled three times the three agreed.

Nothing depends on the number. Its bound is 1 000 000 polls, so no run has come
within four orders of magnitude of the refusal path. It is recorded because it was
first written as a property of the part and is not one.

## The tick back end reaches the converter, Friday 9 October 2026

The factoring's second half. `sampling-systick` calls the shared bring-up instead
of carrying a `TO BE CONFIRMED` block, and this is a **new claim** rather than a
move, so it gets its own prediction and its own capture.

### What it contributes, and it is one thing

**The software trigger.** With `EXTEN` clear no external edge is selected and a
conversion starts when `ADSTART` is written, which is what the tick handler does.
That is the whole mechanism of build 1 and the reason it is expected to lose on
jitter: the sampling instant is produced by software, so it carries exception entry
and whatever higher-priority work is in progress.

`EXTSEL` is passed as zero and means nothing while `EXTEN` is clear. It is passed
explicitly rather than left out, so the step report prints a value a reader can
check against the register.

### Two defects found by reading this file, which had never run

**The `TO BE CONFIRMED` block was right when written and is now unnecessary.** It
listed the converter, the channel, the kernel clock and its prescaler, the boot and
calibration sequence and the resolution bits, and said none was known. All of it is
known, and this file no longer has to know any of it.

**And the conversion start was a read-modify-write on a read-set register.** It was
`ADC1->CR |= ADC_CR_ADSTART`. `CR` holds seven read-set bits where writing a one
re-asserts and writing a zero does nothing, so a `|=` writes a one back into every
one of them that happens to be set. **With only `ADEN` set that is harmless, which
is exactly why it would have appeared to work**; with `ADDIS` or `ADSTP` set it
would re-assert a disable or a stop inside a function whose name says start. It now
uses the mask discipline `adc_bringup.h` documents, which is the same line the timer
back end uses to arm.

That is the second time this week a defect has been found by reading a file before
copying into it rather than by running it.

### The prediction, published before the build

Everything in the step block must match the timer build **except the trigger step**,
because the converter is the same converter and only the trigger differs:

    adc trigger and 16 bits    ok       wrote 00000000  read back 80000000
        started by software, one conversion per tick

The timer build reads `800005A0` there. `EXTSEL` 13 sits at bit 5 and `EXTEN` 1 at
bit 10, which is `0x1A0 | 0x400`, the `5A0`. **Clearing both must leave
`80000000`**, and that is the arithmetic this prediction rests on rather than a
hope.

Three other differences are expected and are not the trigger. The banner says
`acquisition systick` and carries its own image size. There is no `tim6 clock` line,
because only the timer back end prints one. And `convovr` reads **-1**, because this
build does not watch the converter's overrun flag.

**What would refute it.** A refusal at the trigger step, which would mean the
parameterisation rejects a zero `EXTSEL`. A read-back other than `80000000`, which
would mean the field positions are not what the timer capture implies. Or a refusal
anywhere earlier, which would mean the shared bring-up depends on something the
timer back end supplied and this one does not.

### What is deliberately not in this commit

**`OVRIE` for this back end.** Watching the overrun flag here is four lines, and the
old reason for not doing it, that this back end had never been brought up, is no
longer true. It is left out because this commit's claim is that the eight steps run
here, and a second new claim in the same flash would make a failure ambiguous
between them. That is the same reason the move and this were kept apart.

### The tick back end on the board, and the prediction held in every part

**The first back end other than the timer to run on the NUCLEO-H7A3ZI-Q.**

    adc trigger and 16 bits    ok       wrote 00000000  read back 80000000
        channel 13 on PC3 at CN9 pin 5, 810.5 cycles, 16 bits,
        started by software, one conversion per tick
    converter overrun  NOT WATCHED by this back end, so convovr
                       reads -1 rather than a zero nobody earned
    seq 15  blocks 15  mean 3497  overruns 0  convovr -1

`wrote 00000000` and `read back 80000000`, both predicted from arithmetic before the
build. The software-trigger words, the `-1`, and the absent `tim6 clock` line were
predicted too, and all four came out.

**Twenty of twenty-one configuration lines are identical to the timer capture**, the
one difference being the trigger step. So the same converter sequence runs in a
second back end with only the trigger changed, which is what the factoring was for,
established on hardware rather than by reading a diff.

**And it samples.** `seq` and `blocks` rise together by fifteen, `overruns` is 0.

### The size deltas cashed in an earlier reading that could have contradicted them

| target | before | after | delta |
|---|---|---|---|
| `p06-sampling-systick` | 10 232 B | 14 396 B | **+4164** |
| `p06-sampling-timer` | 15 196 B | 15 196 B | **0** |
| `p06-sampling-dma` | 10 248 B | 10 248 B | **0** |

Two commits earlier the shared file cost `systick` and `dma` exactly nothing, and
the reading offered was that `--gc-sections` drops it because neither **calls** it,
with the note that growth in either would mean the linker was not collecting it.
**`systick` now calls it and grows by the size of the thing, and `dma` is still
exactly 0.** One shared file, three targets, costing nothing where unused and 4164
bytes where used.

**The timer image is unchanged to the byte** although `apb1_divisor` moved between
translation units in the same commit. Moving a function across a translation-unit
boundary cost zero here, which is worth having on record before anybody argues that
option C would be expensive.

### And the stamp's documented weakness showed itself, which was not planned

| image | `image` | `main.c built` |
|---|---|---|
| timer | 15 196 B | Oct 9 2026 09:31:08 |
| systick | 14 396 B | Oct 9 2026 09:31:08 |

**Two different images carrying the same compile stamp**, because `main.c` did not
change between them so `__DATE__` and `__TIME__` did not move. That is precisely
what the comment at that `printf` says will happen, written the day before.

The flash figure tells the two apart and the timestamp cannot. **That is why both
lines exist and why the weaker one is labelled for what it cannot detect**, and it is
now demonstrated on the board rather than asserted in a comment. A stamp whose
limits are written down is worth more than one that is trusted.

### What this does and does not do for the chapter

**Two of three mechanisms now reach the converter.** That is not two of three data
points for the comparison, because the comparison is about jitter and nothing here
has measured an interval. `sampling-systick`'s prediction, that it loses on spread
and worst case because its sampling instant is produced by software, is untested and
stays untested until the MCC 118 watches PB4.

What is established is narrower: the mechanism runs, the shared sequence is genuinely
shared, and the build expected to be the baseline now exists to be compared against.

### The witness is live, and the first capture refuted the marker, Friday 9 October 2026

**The chain ran end to end for the first time, and the first thing it measured was
a defect in this project's own marker.** The full record is at the end of
`MEASUREMENT.md`; this is the short form and what it changes in the code.

**Bringing the witness up found a fault that was not in the wiring.** `hat_list`
reported the MCC 118 and that proved less than it reads as proving: it returns the
EEPROM record `install.sh` wrote and does not touch the bus. The first real open
failed with `Board not responding`, as `bing` and as root alike, because
`/boot/firmware/config.txt` lines 61 and 62 gave both SPI0 chip selects to CAN
controller overlays from the other volume this Raspberry Pi serves, `mcp251xfd` on
`spi0-1` and `mcp2515` on `spi0-0`. Each replaces the `spidev` node on its chip
select, so `/dev/spidev*` did not exist with `dtparam=spi=on` three lines above,
and `dmesg` showed both CAN drivers failing to find chips that are not there. Both
lines are commented out with the original kept as
`config.txt.before-mcc118-20261009`, and **the other volume needs them back.** After
a reboot the HAT answers: serial `DBA2019`, firmware 1.03.

**The capture was attributable by the flash figure this time.** Copying the image
and opening the console as one step caught the banner, `image 15196 B`, `TIM6 TRGO
on the rising edge`, `seq` restarting at 15. An earlier attempt the same day opened
the port some seconds later and saw reports from `seq 240` on, attributable by
format and by `convovr 0` but not by the figure. Copy and open as one step from
here.

**The two second shakedown: 102 rising edges, 50.67 Hz fitted, 51.03 Hz counted,
13 ms spread, 1884 missing edges, FAIL on four of five.** The first reading of
that number was mains, and the amplitude column, requested before anything was
committed, refuted it: the floor is two converter codes wide at a driven 0 V,
nothing reaches 1 V, and 346 samples between 0.1 V and 0.49 V are the sampler
catching fragments of a nanosecond pulse. The 102 edges are the catches that
cleared a threshold set from that half-volt swing, randomly placed on the 10
microsecond grid, which is also why the spread is 13 ms on a 19.6 ms mean. 50.67
against 50 was a coincidence.

**The cause: `marker_pulse()` was two consecutive stores, a set and a clear, so the
pin was high for tens of nanoseconds, and the witness samples every 10
microseconds.** The arithmetic needs no instrument. And the record had said so all
along without the code following it: `marker.c` line 3 says "Toggled once per
sample", `MEASUREMENT.md` says "toggles one pin once per sample", and the handler
comment says "one edge per CONVERSION". A pulse is two edges per conversion and a
toggle is one. The code was the outlier against three statements of intent, and it
had never been watched by anything that could tell.

**The fix.** `marker_toggle()`, one store alternating set and reset with the level
kept in a static so the port is never read; all four analysers count both
polarities with the Schmitt pair as hysteresis, `marker_edges` in Python, C++ and
Rust and `rate_marker_edges` in C; both synthesisers render a toggle. The nominal
stays 1000 edges per second, now carried as a 500 Hz square wave, and **no
criterion changes.** The Python suite is unchanged in count, 204 passing and 146
skipping, and the mutation was run: the old rising-only detector on the new
synthesiser reads **500.000 Hz and FAIL**, the new one 1000.000 Hz and pass. The
three compiled analysers are edited blind and are checked only in WSL on win11
skyhorizon; their pre-existing cap of 4096 edges is unchanged and the Raspberry Pi
analysis is `rate.py`, which has none.

**Also fixed while there:** `scan.py` printed an analysis command with `rate.py`
in the wrong directory; it now prints both paths in full.

**What the shakedown established despite its verdict:** the HAT scans at 100 kS/s
without overrun, both scripts run on the Raspberry Pi from the clone, and the
verdict machinery refuses on garbage rather than passing it. The witness is live;
what it watched was wrong. **The prediction for the next shakedown is written in
`MEASUREMENT.md` before it runs**: about 2005 edges in two seconds at about 1002.8
per second, criterion 1 failing again by about 2818 ppm, zero missing edges.

### The first valid capture, Friday 9 October 2026: three of four criteria pass

Two seconds on the toggling image, 2003 edges, analysed on the Raspberry Pi:

| criterion | reading | verdict |
|---|---|---|
| 1, mean rate within 0.1 percent | 1002.6204 ± 0.0002 Hz, +2620 ppm | **FAIL** |
| 2, interval spread below 10 µs | 3.39 µs, below half the sample period, so the instrument's floor | **pass** |
| 3, no interval more than 50 µs off | 9.72 µs worst | **pass** |
| 4, no dropped or doubled edges | 0 of 2003 | **pass** |

**Criteria 2, 3 and 4 have their first readings ever, and the timer build passes
all three.** Its own prediction, written before any capture, was that it would do
well on spread because the sampling instant is made in hardware; the spread it shows
is the witness's own floor, which is the best result this instrument can report.
Criterion 1 fails on the oscillator, as it did by the host clock on Thursday
8 October 2026.

**One prediction missed by an order of magnitude, and the miss is the next
measurement.** The rate was predicted at about 1002.8 Hz within "a few tens of
ppm" of the host-clock figure of Thursday 8 October 2026. It read 1002.62,
198 ppm away. Either the two
instruments disagree, or the RC oscillator moved between the days; the 60 second run
with the stamped console alongside is the measurement that tells them apart, and
`MEASUREMENT.md` records both outcomes and what each would mean before it runs.

The toggle cost 24 bytes in each image that calls it, 15 220 B for the timer image
now, and `rate.py`'s report line no longer says "rising" of a count that is both
polarities.

### The 60 second runs, the host clock beside them, and a sentence withdrawn, Friday 9 October 2026

**Two 60 second captures on the timer image.** The valid one: 60 165 edges,
1002.8038 ± 0.0001 Hz, spread 3.52 µs at the instrument's floor, worst interval
9.81 µs, zero missing; criteria 2, 3 and 4 pass, criterion 1 fails. The one before
it read 1002.906 Hz with one 660 µs interval and one missing edge, and **it no
longer exists**: the second run reused `--out run-003` and `scan.py` overwrote it
without a word. That excursion survives in a pasted report and is marked not
reproducible; `scan.py` now refuses an existing output, with a test.

**The host clock ran alongside for 637 s**, valid by the stamped method: one
banner, 667 reports, every step 15, 1002.924 Hz. Before comparing, the method's
resolution was measured from its own stamp intervals, 28.3 ms standard deviation
over 666 of them: **44 ppm over ten minutes, 470 ppm over one.** That is thirty
times coarser than I had assumed, it makes the per-minute figures noise, and it
withdraws the sentence of Thursday 8 October 2026 that the console run was the
most accurate frequency measurement in the volume: the HAT resolves 0.1 ppm in two
seconds. The host's merit is a traceable clock.

**120 ppm between host and HAT is 2.7 standard deviations of the host: neither
agreement nor a demonstrated offset.** The HAT's own three readings today spread
285 ppm at 0.1 ppm each, which is real and belongs to the oscillator or to the
HAT's clock; two HAT captures in one powered window, minute one and minute nine
after a reset, is the measurement that says which, and needs no host. **Criterion
1 fails by every route**, 2600 to 2900 ppm above the claim.

**The console is shared as the board is.** Two attempts were refused on COM13 by
another reader, the other volume's; `MEASUREMENT.md` now covers the port as well
as the flash.

### The tick build through the same witness: the same numbers, Friday 9 October 2026

| criterion | timer, valid 60 s | systick, valid 60 s |
|---|---|---|
| 1, mean rate | **FAIL**, 1002.804 Hz | **FAIL**, 1002.818 Hz |
| 2, spread | **pass**, 3.52 µs, the instrument's floor | **pass**, 3.51 µs, the instrument's floor |
| 3, worst interval | **pass**, 9.81 µs | **pass**, 9.79 µs |
| 4, missing edges | **pass**, 0 of 60 165 | **pass**, 0 of 60 164 |

**At 10 µs resolution this witness cannot tell a hardware trigger from a software
one on this board.** That was one of the two outcomes written before the run, and it
is the chapter's comparison for now: a null result with its reason, which is a
finer instrument. The standing prediction that the tick build loses on spread and
worst case is not decided, because the quantity it is about sits below the floor.

**The first 60 s run on this build had one 1.47 s gap, and it was the probe.** The
edge before it came 454 µs early, the pin was driven high at 3.24 V for 1.47 s, then
low with no edges for 117 ms, then a rise and regular toggling: a brief halt, the
core held while the SWD port was in use, a reset, and the image booting through its
banner and bring-up. A stalled handler could do the hold and nothing else. The
image that booted toggles PB4, and the other volume's applications do not drive it,
so it was a P06 image; who held the port at 15:55:56 local is the open line. The
repeat with the host stamping alongside was clean, console and pin together.

**The rule that earned:** the stamped console runs beside every capture, and the
other session is asked to be quiet for its length; pre-flight item 12.

### The oscillator settles after a reset, Friday 16 October 2026

Two 60 second captures in one powered window, started 17 s and 8 min 48 s after
the reflash, with the console stamped alongside and clean: **1002.7077 Hz at minute one, 1002.8069
Hz at minute nine, 99 ppm apart at 0.1 ppm each**, and a capture from minute six
of another window within 0.7 ppm of minute nine. A crystal-clocked HAT cannot do
that, so the HSI runs about 100 ppm low in the first minute after a reset and is
flat by minute six. **Last week's 285 ppm spread was when each capture was taken,
not the instruments**, and the question left open on Friday 9 October 2026 is
closed. Criterion 1 still fails at every minute. A rate quoted for this board now
says how long after the reset it was taken.

**A capture was lost a second time to the same overwrite**, because the Pi's clone
had not been pulled since before the refusal was committed; its own "rising edges"
label said so. `git pull` opens every Pi hand-over now. And `scan.py`'s
`firmware_identity` field named the wrong thing, the Pi clone's commit; it is
`analysis_identity` now, and the firmware's identity on the Pi is the banner's flash
figure, taken as `--image-bytes`.

### The transfer-engine back end's buffer was placed by no rule at all, Friday 16 October 2026

Found by reading `acq_dma_double.c` before starting work on it, in code that has
never run, and it is the whole of that file's own warning happening to that file.

Its sample buffer carried `__attribute__((section(".dma_buffer")))`. The linker
script collects `.dma_buffers` and `.dma_buffers*`, and **an ld pattern ending in a
star matches names that START with it**, so a name one character shorter matched
neither. **No rule in the script placed that buffer.** An orphan section goes where
the linker likes, and on this part that is not cosmetic: the file's header calls the
placement the most important comment in this repository, because the main transfer
engines cannot reach the tightly coupled memories at all, so a buffer that lands
there moves nothing and reports no error rather than failing.

**The one section attribute in the volume whose correctness is load bearing was the
one that did not match**, and the defect was a missing letter. The other two,
`.isr_vector` and `.text.p09pad`, match their rules.

**The check that would have caught it now runs on every push.**
`python/tools/check_section_placement.py` compares every `section(".name")` attribute
in first-party C and C++ against every `*(pattern)` rule in the linker script, with
ld's glob rules, and names the near-miss pattern when one is unmatched so the fix
needs no search. It was run before the fix and reported exactly this, which is the
mutation in its natural direction: the defect was the mutation, and the regression
test failed before the one-character change and passes after it.

**What the check cannot do, said where the constant is.** It answers whether any rule
claims a section, not which region the section lands in: a rule collecting a transfer
buffer into DTCM would pass it and be exactly as wrong. The map file from a real build
is the authority on the region.

**And the map file was then read, which is the authority confirming the fix.** Built
on win11 skyhorizon the same evening: `buffer` at **0x24000000**, size 0x100, inside
`.dma_buffers` at the same address with `_sdma` to `_edma` 256 bytes apart, which is
AXI SRAM and reachable by the main transfer engines; `arm-none-eabi-nm` reports it as
`b`, zero filled, so the section is `NOLOAD` as the script intends. The build's region
report shows `.dma_buffers 256` where every earlier build showed 0.

**The image also got 256 bytes smaller, and that refuted a prediction of mine.** The
size table in `docs/building.md` predicted this target would not move; it went 10 248
to 9992 B. A section attribute on uninitialised data makes the compiler emit a loaded
section of zeros, so the orphan was costing 256 bytes of image, and collected into a
`NOLOAD` section it costs none. **An inference that the orphan had been in DTCM is
withdrawn**: the figure proves it was loaded and no longer says where it lived,
because that build's map file was not kept.

### The transfer-engine back end's engine, stream, request and interrupt, Friday 16 October 2026

Settled from ST's own example for **this board**, not for the family:
`Projects/NUCLEO-H7A3ZI-Q/Examples/ADC/ADC_DMA_Transfer` in `STM32CubeH7`, whose
`stm32h7xx_hal_msp.c` configures `DMA1_Stream1` with `DMA_REQUEST_ADC1`,
peripheral to memory, half-word on both sides, and enables `DMA1_Stream1_IRQn`.
`DMA_REQUEST_ADC1` is **9** in `stm32h7xx_hal_dma.h`, defined unconditionally for the
whole H7 family as the DMAMUX1 ADC1 request. Two sources that agree, one of them
specific to the part.

**One of the four is a choice and not a lookup**, and separating them matters or the
next reader treats the example as a constraint. On a part with a request multiplexer
any stream can serve any request, because the multiplexer channel belonging to the
stream is written with the request number: **stream 1 is ST's choice and stream 0
would be equally correct.** What is not free is the request number, which is the
converter's identity on DMAMUX1, and the engine: DMA1 and DMA2 take DMAMUX1 requests
and reach AXI SRAM, while BDMA sits in the other domain behind DMAMUX2. The file
keeps stream 1 only so that a reader comparing it with ST's example is not asked to
track a difference that means nothing, and the placeholder handler named
`DMA1_Stream0_IRQHandler` is now `DMA1_Stream1_IRQHandler` to match.

**What remains, and it is narrower than it was.** The example uses circular mode with
one buffer; this build wants double buffer mode, which the HAL exposes as
`HAL_DMAEx_MultiBufferStart_IT` taking a second memory address. The registers behind
it, the double buffer and current target bits in the stream's control register, the
second memory address register, and the transfer complete flag with its clear
register for stream 1, are RM0455 tables and the device header's bit definitions.
They are not written until they are read, and the handler is still a shell. The
converter bring-up itself is no longer on that list: it is a call to `adc_bring_up()`
exactly as the other two back ends make it.

### The transfer-engine handler had no vector, which is the second time that cost this project, Friday 16 October 2026

Renaming the handler to `DMA1_Stream1_IRQHandler` prompted a one-line look at
`c/board/startup.c`, which had **no DMA entry of any kind**. That handler has been in
the tree since Saturday 3 October 2026 and could never have run, whatever the engine
configuration said.

**This is the failure that cost an evening on Wednesday 7 October 2026**, recorded at
length in `startup.c` and in `c/board/stm32h7a3_regs.h`: the converter bring-up
reported all eight steps ok, every register read back what was written, `acq_start`
returned 0, and no sample ever arrived, because `ADC_IRQHandler` was declared,
compiled and linked and was not in the table. **Three things must be true for a
handler to run, the peripheral's own enable in its IER, the interrupt controller's
enable through NVIC, and a vector for the handler to reach, and only the first two
read back.** The third has nothing to read back from, which is why it is the one that
goes missing.

**The position is sourced, not guessed.** `DMA1_Stream1_IRQn` is 12 in ST's
`IRQn_Type` for this part, read from `cmsis_device_h7`, and the three positions this
repository already recorded from the pack on Wednesday 7 October 2026 were re-read
from the same copy in the same pass and all three agree, 18, 39 and 54. Stream 0 at 11
is recorded beside it, because on a part with a request multiplexer the stream is a
free choice and the neighbour is what a later change would reach for. The slot costs
nothing in size, since the array's extent is set by the highest index and USART3 at 39
is far above 12, and for every image that does not define the handler the entry is the
weak alias to `Default_Handler`, which is a better answer to a spurious interrupt than
the zero vector an unlisted slot leaves.

**And the entry was added before the back end is configured, deliberately in that
order**, because the omission has now happened twice and the configuration is the part
that would make a reader think the work was done.

**The rebuild then said something stronger than the finding did.** Adding the
entry was predicted to change no image's size, since the table's extent is set by
its highest index; `.isr_vector` did stay 224 bytes in all three images rebuilt,
and `p01-first-light` and `p06-sampling-timer` did not move. **The transfer-engine
image grew 84 bytes**, 9992 to 10 076, because the build compiles with
`-ffunction-sections` and links with `-Wl,--gc-sections`, so until the table
referenced it nothing did, and the linker had been discarding that handler: **it
was not in the image at all.** The finding said the handler could never run; the
arithmetic says it was not there to run, and the 84 bytes are its code arriving
for the first time since Saturday 3 October 2026. **`nm` closes it with no slack**:
that handler is `T`, a real definition, of exactly 84 bytes, so the entire delta is
one function and nothing arrived beside it.

**The check runs on every push**, `python/tools/check_handler_vectors.py`: every
`*_IRQHandler` DEFINED in first-party C against the designated indices in the table,
with the `IRQ_` naming convention the table already uses, and it also reports an index
name that no `#define` binds. **Its own first version under-reported and that is
recorded in it**: the pattern began with a class that matches a newline, so a
definition with a blank line above it was dropped in silence, which is how it found
two handlers where the repository defines three. `ADC_IRQHandler` has a blank line
above it. It was caught by comparing the tool's output against a grep by hand, the
blank-line case is now one of its eight tests, and the regression guard demands at
least three handlers so the same under-reporting cannot return quietly.

**What it cannot do, said where the code is.** It checks that an entry exists and
names the handler, not that the POSITION is right: a vector at the wrong index routes
the interrupt to the wrong handler and passes. The positions are cited one by one in
the register header, and the generated table with one test per row that the
improvement plan describes is what would check them.

### The transfer-engine back end is fully specified from ST's own material, Saturday 17 October 2026

Every register this back end needs is now read and cited in `acq_dma_double.c`, and
none of it is written yet: reading came first on purpose, and the code follows in one
piece once the decision below is made.

**One of the readings changed the design.** On this part the converter raises no
transfer request unless `CFGR`'s `DMNGT` field says so, 00 leaving data in `DR` alone
and 11 selecting circular, from `ADC_CONVERSIONDATA_*` in ST's `stm32h7xx_hal_adc.h`.
**The shared bring-up leaves `DMNGT` at 00**, which is right for the timer and tick
back ends and wrong for this one, so this file owes one converter write of its own,
reported in the same wrote-and-read-back form as the eight shared steps. Without it
every other register here would be correct and nothing would ever move, which is this
project's characteristic failure.

**The rest, with its sources.** The request routing is `DMAMUX1_Channel0` plus the
stream number, so channel 1 for stream 1, written with the request value 9, from
`HAL_DMA_Init`'s own arithmetic in `stm32h7xx_hal_dma.c`. Double buffer mode is `DBM`
in the control register with `M1AR` taking the second address, then `NDTR`, `PAR`,
`M0AR`, the interrupt enables and the stream enable last, in the order
`HAL_DMAEx_MultiBufferStart_IT` writes them. Which half finished is the opposite of
the one `DMA_SxCR_CT` names, from `LL_DMA_GetCurrentTargetMem`. The flag is
`DMA_LISR_TCIF1` from `LL_DMA_IsActiveFlag_TC1`, **named per stream in the device
header so no bit arithmetic is needed and none is done**; its clear macro's name is
the one thing in the block not quoted from a file read, and a wrong macro name is a
compile error rather than a silent failure.

**And one decision is not mine.** This back end needs TIM6 running at the sampling
rate exactly as the timer back end does, and that setup is inline in `acq_timer.c`.
Three ways: duplicate it here with the duplication named, as option A did for
`apb1_divisor` on Friday 9 October 2026; factor it into a shared `tim6_bring_up()`
beside `adc_bringup.c`, checked the same way by counting console-affecting strings
before and after; or leave this back end refusing until a wider decision about what
else shares a timer. **I would choose the factoring**, because the converter
bring-up's paid for itself within two days and the check that made it safe already
exists, and it is recorded as a question because the last one of these was Joseph's.

### The grounded control reproduces nine days later, and the wire became an instrument, Saturday 17 October 2026

Four captures of the timer build in one session, all from the same 15 220-byte image
with nothing rebuilt, taken because the first of them showed `mean` at about 3500 and
**the record already had a sentence for that**: a floating PC3 reads near 3200, and a
different `mean` means the wire moved rather than that the firmware did.

**The control passes, and it is the first time this project has reproduced one of its
own controls on a later date.** `mean` reads **1 to 2 across 75 reports**, which is
the same band and the same spread of one count as Thursday 8 October 2026, nine days
and an unknown number of bench disturbances earlier. The threshold was the one
published then and not rewritten now: within a few counts of 0 and steady.

**And the intermediate step turned one wire into a measurement nobody asked for.**
The PC3 lead went first to a ground terminal on the **MCC 118**, which is a ground on
a different board, and only afterwards to a **GND pin on CN11** beside PC3 at pin 37.
Three points result, all at 16 bits against a 3.3 V reference, so one count is
50.4 microvolts:

| PC3 connected to | `mean` | As a voltage | Reports |
|---|---|---|---|
| nothing, floating | 3301 to 3857 | 166 to 194 mV | 76 |
| a GND terminal on the MCC 118, another board | 94 to 165 | 4.7 to 8.3 mV | 78 |
| a GND pin on CN11, its own board | 1 to 2 | 0.05 to 0.10 mV | 75 |

**The middle row is a reading of the offset between two boards' grounds**, and it was
obtained with no instrument beyond the converter already under test. Four to eight
millivolts is small, and its smallness is itself informative: a lead to a ground that
was not bonded at all would be expected to sit further away and to drift, so the
figure is consistent with the two grounds being joined and separated by a few
millivolts of return path. **What it does not do is prove that bonding**, because this
capture cannot distinguish a bonded ground a few millivolts away from an unbonded one
held close by leakage, and the state of the section 4.4 ground lead at the time was
not recorded. The three outcomes were written down before the 08:52 capture ran, and
the one that happened, tens to low hundreds, was the third of them.

**Why the middle row matters beyond curiosity.** The witness measures a marker pin
against the MCC 118's ground. An offset of 8 mV on a 3.3 V logic edge is 0.24 percent
of the swing, which is three orders of magnitude below the 0.1 V hysteresis the
analysers use, so it cannot move an edge count. That is now a figure rather than an
assumption, and it is the sort of thing that is cheap to measure while the wire is
already in hand and expensive to guess at later.

**What these captures settle about the converter, independent of the wire.**

`OVRMOD` **is 0 as found**, printed in seven banners across all four captures. That
line existed in no committed file in this repository before today, and it is the
baseline the open decision needs: with `OVRMOD` clear the data register is preserved,
which is exactly what `acq_timer.c`'s handler argues from and names as the value it
reports rather than sets.

**Both poll counts held still within the session and are still known to move between
sessions.** Calibration took 180 polls and the enable was ready after 4 polls in every
one of the seven banners. This page has read 187 for calibration on another evening,
so the honest statement is that neither figure is a property of the part and both were
stable across seven boots of one session.

**`overruns 0` and `convovr 0` in every report of every capture**, about 300 of them,
with the overrun interrupt enabled and `IER` reading back `00000014`.

**And the documented truncation reproduced, which is a third instance.** One banner
cuts off mid-word at `moves only wh` and the next begins immediately, because a reset
landed inside the roughly ten seconds the report takes to leave the port. Section 1.5
of `docs/the-board-and-the-wiring.md` warns about it from two earlier captures that
lost their core frequency line. Harmless here, since the next boot reprinted
everything, and worth a third mention only because the warning has now earned its
place three times.

### The build stamp cannot identify this image, and the gap is exactly where this project was hurt

The banner reads `main.c built Oct  9 2026 09:31:08` while the image file is dated
Friday 16 October 2026 at 23:47. The stamp is eight days older than the image and its
own comment says why: **it moves only when `main.c` recompiles.**

So neither the stamp nor the `15220 B` size distinguishes this image from one built
before Friday 16 October 2026, and the change made that evening was the **vector table
entry in `startup.c`**, a different translation unit whose addition left this image's
size unchanged. A reader of these captures cannot tell from the banner whether the
flashed image carries that entry.

**That is the gap in the worst possible place.** This project lost an evening on
Wednesday 7 October 2026 to a handler with no vector, in `startup.c`, and the
self-identification the volume adopted afterwards cannot see changes to `startup.c`.
It is recorded here rather than fixed because the fix is a decision about what the
banner should carry, and the candidates differ in cost: a `git describe` string
compiled into the image names the commit exactly and makes every image depend on the
working tree's cleanliness; a stamp per translation unit is cheap but verbose; and the
image size already printed is a weak discriminator that happens to be blind in this
case. Nothing here is urgent, because the three functional claims above do not depend
on the vector entry, which only matters to the transfer-engine build.

### The TIM6 move is verified on the board, and the transfer engine compiles, Monday 19 October 2026

The factoring of Monday 19 October 2026 was judged by a criterion published in
`tim6_bringup.h` before the move was made. **It is met.** All twenty-four
configuration read-back lines and the timer clock line above them came back identical
on the board, hex for hex, against the capture committed from Friday 16 October 2026:
`as found 20000000`, `read back 00030000`, `10000100`, `00001001`, `F3FFFFFF`,
`00000E00`, `00000340`, `800005A0`, `00000014`, `10000105`, and both poll counts at
180 and 4. A pure move changed no register write and no format string, and that is
now a measurement rather than an argument from a diff.

**One line differed and was excluded from the criterion before the capture, not
after.** The banner prints the image size, and the image grew 56 bytes because the
moved code can no longer be inlined into `acq_start`, so it reads 15 276 B where it
read 15 220 B. The original wording in that header said the console OUTPUT must be
byte-identical, which was too strong; it is corrected there with the reason, and the
correction is dated before the run.

**The three size predictions were written down before the build and all three held.**
The timer build grew slightly at +56 bytes, the tick build did not move at all from
14 420 B because it links `tim6_bringup.c` and calls nothing from it so the linker
discards both functions, and the transfer-engine build grew substantially at +5736
bytes. `docs/building.md` carries the table.

**The transfer-engine back end compiles**, which is the first thing that has ever been
true of it. Its map puts `.dma_buffers` at `0x24000000`, 256 bytes, in AXI SRAM where
the engines can reach it. **It has still never run**, and nothing here claims it does.

### The mean swung by a factor of three, and the move is excluded as the cause

The first post-move capture read `mean` from 9364 up to 10323, dropped to a band
around 3600 to 5899, returned, then settled at 3052 to 3409 for the rest of the run.
Against two pre-move captures that sat at 3039 to 3438 throughout, that looks exactly
like a regression caused by the code.

**It is not, and two independent facts exclude it.** The converter's configuration is
identical line for line, so the channel, the preselection, the sampling time, the
resolution, the trigger and the kernel clock are all unchanged. And the grounded
control was repeated on the post-move image: `mean` 2 to 6 across 77 reports, against
a threshold of within a few counts of zero published on Thursday 8 October 2026, where
the control of Saturday 17 October 2026 gave 1 to 2. A converter that is sampling
wrongly does not return to a few counts of zero when its pin is tied to ground.

So `mean` was reading pickup on a floating PC3, which this file has recorded as
meaningless since Thursday 8 October 2026, when it changed by three orders of
magnitude between two nights with nothing deliberately connected. **What this episode
adds is the discriminator rather than an explanation**: the grounded control settles
in one capture whether an unexplained analogue reading implicates the firmware, and it
cost one wire and seventy-five seconds. No cause is offered for the swing itself, and
one observation would not support one.

### A third class of field, and a comment that pointed at nothing, Saturday 17 October 2026

The `DMNGT` finding above raised a question it did not answer: if one field in the
shared bring-up is wrong for a back end that inherits it, which others are
per-back-end decisions frozen at one value? Asking it of `adc_bringup.c` found two
things, both by reading.

**The file's own taxonomy of its fields was incomplete.** It names two classes, the
values the caller picks, which are the trigger's, and the values the file chooses
because they are the same for every back end, which are the resolution and the
sequence length. There is a third: **the fields it leaves at their reset value, which
are not all the same for every back end.** Step 7's write is the only write to
`ADC1->CFGR` in this repository and it masks `RES`, `EXTSEL` and `EXTEN` and nothing
else, so every other field of that register is whatever reset left. `DMNGT` is one.
`OVRMOD` is the other, and it decides what an overrun does to the data.

**`OVRMOD` was right to be a reading and is now a decision, because of who reads the
data register.** `acq_timer.c` reports it as found and declines to choose, which was
correct for a value nothing depended on. Its handler then wrote a correctness
argument that does depend on it, and named the dependency rather than hiding it: with
`OVRMOD` clear the data register is preserved, so until `OVR` is cleared the converter
keeps discarding and the stream stops for good while every register still reads back
correctly. For a back end whose **handler** reads `DR` that argument holds. For one
whose **engine** reads `DR` it is a different question, so the field is now an open
decision recorded in both files rather than a reading. The dependency being named
where it is relied upon is why this is an open decision and not a defect.

**Enumerating the rest needs the vendor header, which is not on the authoring
laptop.** Every converter register name in this project comes from ST's
`stm32h7xx.h`, which arrives with the pack on win11 skyhorizon and in continuous
integration, so the complete list of `CFGR`'s fields and their reset values cannot be
read where this was written and is not guessed. The two named above are named because
both were read.

**And a comment pointed at a line that is not in its file.** `adc_bringup.c` said the
`OVRMOD` line was "later in this file"; there is no `OVRMOD` line in that file at all.
Git says the sentence was written on Thursday 8 October 2026 in `acq_timer.c`, where
it was true, that file then holding five `OVRMOD` mentions. The move of Friday 9
October 2026 brought the regulator-flag half here and left the `OVRMOD` print there,
so **the pointer travelled with the half that moved**. The move's published criterion
was that every configuration read-back come back identical line for line, and a
comment cannot fail that check, so nothing verified this and nothing could have.

**It is not worth a checker and that was measured rather than assumed.** Eleven
comments in first-party C claim something is "in this file"; nine name no identifier
at all, and of the two that do, the other one is sound, both its names appearing four
times in their own file. A checker for a population of two, catching one, would cost
more than reading does.

### The transfer-engine back end's first run, specified before it happens, Tuesday 20 October 2026

Written while preparing the run and before any of it exists, so no criterion can
be adjusted to fit an answer. It follows the form used for criterion 1's attempt
further up this page.

**What the run is.** Build `p06-sampling-dma`, flash it, and read COM13 at 115200
in the same command, because the board is shared and a console opened after the
copy misses the banner. Press the black RESET button while the reader counts. Let
it run for at least 120 seconds with PC3 grounded at CN11 pin 37.

**What the output must contain before any figure is read.** The banner naming
`acquisition   dma`, then the input pin and the eight converter steps, then this
back end's own seven: data management circular, dma1 clock, dma stream idle,
dmamux request 9, dma stream configured, dma both halves, dma stream enabled.
Every one prints what it wrote and what the register held. **A refusal on any of
them ends the run there**, and the line that refused is the finding.

**The criteria, each able to fail.**

1. **`blocks` advances at all.** This is the whole question for a back end that
   has never run. Zero blocks with all fifteen configuration lines reading ok is
   the specific failure this build is built to make visible.
2. **The report cadence in time is 0.96 seconds per line, within 2 percent.**
   A line is emitted every 15 blocks of 64 samples, which is 960 samples, so at a
   true 1 kHz the lines arrive 1.04 per second. **The cadence in blocks cannot
   fail and is not a criterion**: `main.c` says so in its own comment, after this
   page once published "blocks rises by roughly 15 each line", which is 1000 over
   64 and could not have come out otherwise. The cadence in TIME can fail, and
   half rate or double rate would show as 1.92 or 0.48 seconds per line.
3. **`mean` reads 2 to 6 across the run** with PC3 grounded, the same threshold
   published on Thursday 8 October 2026 and met by the timer build on Saturday
   17 October 2026 and Monday 19 October 2026. A converter that is sampling
   something other than the pin does not return to a few counts of zero when the
   pin is tied to ground.
4. **`overruns` stays 0.** That counter is main failing to collect a finished
   half, and at 15.6 halves per second it has no excuse. A nonzero count would be
   a finding about the application loop rather than about the engine.
5. **`convovr` reads 0 for the whole run, and a 0 here is now earned.**
   Rewritten hours after it was first published, because the decision to watch
   the flag was taken the same day: this criterion said `convovr` reads -1 and
   that a zero would be the defect. The back end now enables `OVRIE` and its
   `ADC_IRQHandler` counts and clears the flag, so the banner prints "watched,
   so convovr below is a real count" and a zero means the flag was watched
   throughout and never set. **A -1 would now be the defect**, because the only
   way to get one is for the interface to report that nobody looked.

**If `blocks` stays at zero, the suspect list is shorter than it was.** `CIRC`
is off it: RM0455 pages 605 and 622 say the hardware forces that bit when double
buffer mode is set and the stream is enabled. What remains, in the order the
console can answer it: the data management field, whose read-back is printed; the
multiplexer routing, whose read-back is printed; the stream's own enable bit,
printed; and **the converter's overrun flag, which this build could not see
when this section was written and now can**. An overrun stops every further
transfer request until software clears it, so a run that produces a few blocks
and then stops had that as its first suspect with nothing in the output to say
so. Since the decision of Tuesday 20 October 2026 the same failure appears as
`convovr` rising while `blocks` stops, which is the difference between a symptom
and a diagnosis.

**What this run cannot settle, and the reason is structural rather than an
omission.** `MEASUREMENT.md` measures criteria 2, 3 and 4 from the marker pin's
edges, one per sample. **This back end toggles the marker nowhere.** It calls
`marker_init` and never calls `marker_toggle`, where both other back ends toggle
in their sampling interrupt. That is not a missing line: **nothing happens in
software per sample in this build**, which is the entire point of it, so a
per-sample edge cannot exist. Its handler fires once per 64 samples.

So the prediction in the three-builds table above, that this build is
indistinguishable from the timer build **on spread**, cannot be evaluated by the
published method as the code stands. That is recorded here rather than discovered
as a flat line on the witness, and the choice is Joseph's:

| Option | What the witness would then measure | What it costs |
|---|---|---|
| Toggle in the block handler | the regularity of block completions, 15.6 per second | the prediction must be relabelled: it would be a claim about blocks, not about samples, and the table's "on spread" would need to say so |
| Leave it untoggled | nothing from this build | criteria 2, 3 and 4 are unavailable for this back end, which must be written into `MEASUREMENT.md` rather than left implicit |
| Drive the pin from a timer channel in hardware | the sample instants themselves, for all three builds | the stretch goal this page already names; it is the only route that separates handler latency from the conversion instant, and for this build it is the only route at all |

**The honest reading of that table is that the third option is what the chapter's
comparison actually requires**, and that the first two are ways of saying so with
different amounts of candour.

#### Joseph took the third option the same day, and it cannot be written yet

**Decided Tuesday 20 October 2026, with the reasoning stated rather than the
choice alone**: do not publish a comparison the instrument cannot perform, and
choose the option that measures the claimed quantity. The table's prediction is
about the spread of sample instants, so the marker has to be driven by the thing
that determines those instants.

**What blocks it is a fact about this part, not a preference.** RM0455 Rev 13
section 46.2 on page 1754: TIM6 and TIM7 are BASIC timers, whose features are a
16-bit auto-reload upcounter, a prescaler, a synchronisation circuit to trigger
the converter, and an interrupt or transfer request on update. **They have no
capture or compare channels and they drive no pin.** TIM6 is the timer that
triggers every conversion in two of the three builds, so the trigger source as it
stands cannot also toggle the marker.

**So the decision implies a second decision nobody has taken**, and it is the one
to settle before any code: either the conversion trigger moves to a timer that
has output channels and can also trigger ADC1, which changes `EXTSEL` for the
timer and transfer-engine builds alike, or TIM6 keeps the trigger and a second
timer is slaved to its output, in which case the marker edge sits one
synchronisation step behind the conversion and the page must say so.

**BOTH LOOKUPS ARE DONE, Tuesday 20 October 2026, and together they remove the
second decision.** `TIM3_CH1` reaches PB4 on AF2, from DS13195 Rev 1 Table 9
page 70, and the converter's trigger list from `stm32h7xx_ll_adc.h` carries both
`TIM3_TRGO` and `TIM3_CH4`. **So one timer does both jobs**: TIM3 triggers the
conversions through its trigger output and toggles PB4 through channel 1, with no
processor in the path and no second timer slaved to the first.

**That is the arrangement the three-builds table has always needed**, and it needs
no change of pin: the marker stays on PB4, which is where Joseph's wire already
sits at CN12 pin 27. What remains is a build, not a question: TIM3 replaces TIM6 as
the trigger for the timer and transfer-engine builds, `EXTSEL` moves from 13 to the
value the LL header gives for `TIM3_TRGO`, channel 1 is configured for output
compare on AF2, and `marker_toggle` leaves the interrupt path for those two builds.
**The tick build keeps its software toggle**, because its whole point is that the
processor is in the path.

**One thing the datasheet added that was not asked for.** PB4 comes out of reset as
`NJTRST`, JTAG's reset input, which the wiring page now records. Driving it as the
marker is repurposing a JTAG pin, which is safe under SWD and is now sourced rather
than reasoned.

**The two lookups as they were posed**, kept because the sourcing matters more than
the answer:

| What must be read | Where it is | Why not from the manual |
|---|---|---|
| Which `EXTSEL` events are timer trigger outputs, and which of those timers have output channels | ST's LL header in the pack on win11 skyhorizon, `stm32h7xx_ll_adc.h`, whose line 993 is where `EXTSEL` 13 for TIM6 TRGO came from; the symbols are `LL_ADC_REG_TRIG_EXT_*` | **RM0455 page 1028 lists `EXTSEL` as "Event 0" to "Event 31" and names no timer at all.** The mapping is not in the register description, which is why this project sourced 13 from the headers in the first place |
| ~~Which timer channel can reach PB4~~ **Answered Tuesday 20 October 2026: `TIM3_CH1` on AF2**, from DS13195 Rev 1 Table 9 page 70 | read from RS Online's mirror after st.com timed out twice; the citation is the vendor footer on the page | Both of UM2408's pin tables were read first and neither gives an alternate function, which is why the datasheet was needed. UM2408 Table 18 page 44 gives PB4 as CN7 pin 19 and the board's Arduino name, not the part's alternate functions. The datasheet is not on this laptop and st.com serves no PDF here |

**Nothing is changed in the code for this decision today, and the reason is not
only the missing documents.** Whether this engine moves a sample at all is prior
to what the marker should mean, and the run specified above answers that for the
cost of one flash. A hardware marker built before that run would be two new
claims in one image, which is the mistake this project named on Saturday
17 October 2026 when it kept the TIM6 move and the overrun watch apart.

**The hand-over, when the bench is free.** On win11 skyhorizon, PowerShell 5.1,
one line that dot-sources the toolchain, builds, resolves the board by its volume
label and opens the console in the same command:

    cd C:\Users\skyhorizon\Desktop\firmware-nucleo-h7; . .\projects\P01-toolchain-first-light\Use-CubeIDEToolchain.ps1; cmake --build build-fw --target p06-sampling-dma; if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED, board untouched' } else { $v = Get-Volume | Where-Object { $_.FileSystemLabel -eq 'NOD_H7A3ZIQ' }; if (-not $v) { Write-Host 'NOT FLASHED: no volume labelled NOD_H7A3ZIQ' } else { Copy-Item build-fw\p06-sampling-dma.bin "$($v.DriveLetter):\" -Force; $p = New-Object System.IO.Ports.SerialPort COM13,115200,None,8,one; $p.DtrEnable = $true; $p.RtsEnable = $true; $p.Open(); 1..240 | ForEach-Object { Start-Sleep -Milliseconds 500; $s = $p.ReadExisting(); if ($s) { Write-Host -NoNewline $s } }; $p.Close() } }

**A second route exists as of Tuesday 20 October 2026 and is unproven for this
project.** A Debian `arm-none-eabi-gcc` 14.2.1 on PATH in WSL on the same laptop
was probed that day and compiles, links bare metal, links newlib and carries the
hard-float multilib. It has never built this project, which needs the vendor
device header at a Windows path reachable as `/mnt/c/...`, so the route above is
the one with six images behind it and the WSL route is a first attempt rather than
a fallback.

### The marker pin is under the shield, and its Morpho position is read, Tuesday 20 October 2026

**Found by Joseph asking where the marker wire goes**, which this volume's wiring
page is supposed to answer without being asked. It recorded that the stacked
X-NUCLEO-53L8A1 makes **CN9 pin 5 unreachable**, so PC3 is wired at CN11 pin 37
instead, and said nothing about **CN7 pin 19**, which is PB4 and the pin every
criterion except the console readings is measured from. **CN7 is a Zio header, not
a Morpho one**, so the same shield covers it. Joseph confirmed by looking at the
board: no jumper can reach it while the shield is fitted.

**So the fallback stopped being optional and the Morpho table was read.** Until
that day every mention of PB4 in this volume gave one position, from UM2408
Table 18, which is the Zio table. **Table 19 on page 47 puts PB4 at CN12 pin 27.**

**The column was resolved by coordinates rather than by reading order**, because a
four-column table flattens into an interleaved sequence that could put PB4 on
either header. PC3 is the anchor: it extracts at x 177 against pin number 37,
which is the CN11 odd column and the position this volume and ST's own converter
example already carry. With the mapping fixed, PB4 at x 367 against pin number 27
is CN12 pin 27, and PB3, known here as D23 and SWO, lands in the same column at
pin 31 as a second check.

**Reachability was the open half and Joseph closed it the same day, by fitting the
wire.** The marker lead runs from **CN12 pin 27 to MCC 118 channel 0** as of
Tuesday 20 October 2026. That matters more than it sounds, because UM2408 section
7.17 calls CN11 and CN12 "two male pin header footprints", **OFF by default**, so a
Morpho position is a fallback only on a board whose footprint carries a header.
This board's does.

**What remains unproven is the path, not the pin.** No run has toggled PB4 since
the wire went on, because this afternoon's was the transfer-engine build, which
toggles the marker nowhere. **The first witness run after this change has to be
`p06-sampling-timer` or `p06-sampling-systick`**, and until one of them puts edges
into the MCC 118 the wire is connected and unmeasured. **The ground lead went in the same day**, from a GND pin on the NUCLEO-H7A3ZI-Q
to the MCC 118's ground terminal, so both wires of 4.4 are now present. Without it
the HAT reads the ground offset rather than the signal, which cost 94 to 165 counts
on Saturday 17 October 2026. **What is still missing is a run**: the timer or the
tick build, since the transfer-engine build drives this pin nowhere.

**Nothing is blocked today.** This afternoon's run used no marker, and the
transfer-engine back end toggles it nowhere. Criteria 2, 3 and 4 for the timer and
tick builds are measured from that pin, so this is needed before the next witness
run and not before.

### The transfer-engine back end ran, Tuesday 20 October 2026

**It moves samples.** 1890 blocks of 64, which is 120 960 samples, across 126
report lines, with `overruns 0` and `convovr 0` on every one of them. The back end
written on Monday 19 October 2026 and compiled for the first time that afternoon
had never run until now.

**The criteria were published before the run and four of the five are met. The
fifth was mis-specified by me and the measurement is better than the criterion.**

| Criterion, as published | Measured | Verdict |
|---|---|---|
| 1. `blocks` advances at all | 1890 blocks, 126 lines | **met** |
| 2. the report cadence is 0.96 s per line within 2 percent | 126 lines against a reader window of at least 120 s, where 120 960 samples at exactly 1 kHz is 120.96 s | **consistent, not established**: this capture carries no per-line timestamps, so the window length is known only as a lower bound |
| 3. `mean` reads 2 to 6 across the run | **1 to 6** | **the criterion is wrong, the threshold is met**: see below |
| 4. `overruns` stays 0 | 0 on all 126 lines | **met** |
| 5. `convovr` reads 0 and the zero is earned | 0 on all 126 lines, with the banner printing "watched, so convovr below is a real count" | **met** |

**Criterion 3 had a lower bound on a quantity whose ideal is zero, which is
backwards.** It was written from the band the post-move capture happened to show,
2 to 6, rather than from the threshold that band was tested against, which is
within a few counts of zero and was published on Thursday 8 October 2026. A reading
of 1 is better than a reading of 2. The criterion should have had an upper bound
only, the measurement passes the threshold it was derived from, and the band
widened downward rather than upward, which is the direction that cannot be a
defect.

#### The manual's claim about CIRC is now measured on the board

This is the result worth more than the five criteria, and it was not one of them.
The configuration read-backs print the stream's control register twice, before the
enable and after it.

    adc dma stream configured  ok  wrote 00042C16  read back 00042C16
    adc dma stream enabled     ok  wrote 00000001  read back 00042D17

**The pre-enable value is exactly the word the code assembles**, `DBM` with `MINC`,
`PSIZE_0`, `MSIZE_0` and the three interrupt enables, which comes to `0x00042C16`
by arithmetic. **The post-enable value differs by `0x101`, which is bit 0 and bit
8.** Bit 0 is `EN` and the code wrote it. **Bit 8 is `CIRC` and nothing in this
repository has ever written it.**

So RM0455 Rev 13 section 15.3.11 page 605 and the bit's own description on page 622,
read on this laptop that morning, are confirmed by the part itself that afternoon:
double buffer mode enables circular behaviour automatically and the hardware forces
that bit as soon as the stream is enabled. **A documentary claim and a register
read-back agreeing is a stronger result than either alone**, and it retroactively
justifies leaving the bit unwritten, which until this morning was only a copy of
ST's sequence.

#### Three other things the run settled or moved

**The buffer is where the linker put it and where the engines can reach it.** The
map reports `.dma_buffers` at `0x24000000`, 256 bytes, which is 64 samples times
two halves times two bytes, in AXI SRAM.

**The build stamp is stale against the image for the fourth time, and in a new
way.** This image reports `main.c built Oct  9 2026 09:31:09` where all twelve
committed captures read `09:31:08`. The one second is not a rebuild: each target
compiles its own `main.o`, and the three were built a second apart on Friday
9 October 2026. **So `main.c` was not recompiled today even though the image grew
to 15 988 B**, which is the same blind spot this volume has recorded for the back
ends, for a change in `startup.c` and for a change that altered the image size.

**Calibration took 181 polls**, which is a third distinct value after 180 and 187.
The claim that this count is not a property of the part is now supported by three
readings rather than by two, one of which had no capture behind it.

#### What the run did not settle, unchanged by it

Criteria 2, 3 and 4 of `MEASUREMENT.md` are about the instants of individual
conversions and are measured from the marker pin's edges. **This back end toggles
the marker nowhere**, by the structural argument above, so the run says nothing
about them and the witness was not used. The hardware marker decision stands where
it stood this morning, and the first of its two lookups is now done: the converter's
selectable triggers come from `stm32h7xx_ll_adc.h`, whose `LL_ADC_REG_TRIG_EXT_*`
symbols name TIM1 CH1, CH2 and CH3, TIM2 CH2, TIM3 TRGO and CH4, TIM4 CH4 and TRGO,
TIM8 TRGO and TRGO2, TIM1 TRGO and TRGO2, TIM2 TRGO, TIM6 TRGO, TIM15 TRGO,
TIM23 TRGO and TIM24 TRGO. **Every one of those except TIM6 belongs to a timer with
capture and compare channels**, so the trigger can move to a timer that also drives
a pin. Which channel reaches PB4 is the remaining lookup and needs the datasheet.

#### The capture's provenance, stated rather than assumed

The figures above were read from the terminal, not from a file the reader wrote.
Every capture committed to `console/` so far was written by a redirecting reader,
and this one was printed to the screen, so **it is not committed here**. A re-run
with the output redirected is two minutes and gives the directory an artefact with
the same provenance as the other twelve. Until then this section is the record, and
it says where its numbers came from.

### What is next, in the order that makes each claim true

1. ~~A build stamp in the banner.~~ **Done Friday 9 October 2026 and verified
   against a criterion that could fail**, in the section above. The flash figure is
   the useful half and the compile time is labelled for what it cannot detect.
2. **Decide what the claim is**, now that criterion 1 has failed on this clock at
   +2818 ppm. Four options are above, the first is withdrawn, and the realistic
   default is to restate the claim as nominal with the measured tolerance beside
   it. There is no option on this board that both keeps the number 1000 and
   passes.
3. ~~The grounded PC3 control.~~ **Done Thursday 8 October 2026 and it passed**,
   closing both observations: the offset was the floating input and the 0.118 Hz
   oscillation was pickup on it.
4. ~~The marker wire~~ **is in, the witness is live, and the first valid capture
   is taken, Friday 9 October 2026**: the timer build passes criteria 2, 3 and 4
   and fails 1 on the oscillator, on two seconds and on sixty. The host clock
   alongside resolved 44 ppm and could not settle the instruments against the
   oscillator; two HAT runs in one powered window did, on Friday 16 October 2026:
   the oscillator settles about 100 ppm upward in the first six minutes after a
   reset. The tick build then
   gave the same four numbers, so the comparison the chapter exists for is a null
   result at this witness's 10 µs floor, with the reason stated. What remains in
   this item is a finer instrument for criterion 2, and three runs per build where
   one exists.
5. ~~**`sampling-dma`**, the last back end with no converter.~~ **Written and
   compiling since Monday 19 October 2026, and it has never run.** Its buffer's
   placement is fixed and confirmed in the map file, its engine, stream, request
   and interrupt were settled from ST's example for this board and its handler got
   a vector, all on Friday 16 October 2026. Every register it needs was read and
   cited on Saturday 17 October 2026. The TIM6 decision that blocked it was taken
   on Monday 19 October 2026, the setup is one shared function, the back end's
   whole configuration is written, and the build grew by 5736 bytes with its buffer
   at `0x24000000` in AXI SRAM. **What remains is its first run**, which is the
   only thing on this page that needs the board rather than a reader.

   Two sentences of this item were removed on Tuesday 20 October 2026 rather than
   struck through, because they were not merely out of date. One said the thing
   standing between the specification and the code was the TIM6 decision, which had
   been taken and acted on. The other said the build still needed its engine,
   stream and request number as RM0455 table lookups, **which this same item's
   first sentence had called settled four days earlier**, and which came from ST's
   headers rather than from any table. An item that contradicts itself is worse
   than an item that is behind.
6. **`OVRMOD` needs no choosing, and what it was standing in for does.**
   Narrowed on Tuesday 20 October 2026 by reading RM0455 Rev 13 rather than by any
   measurement. The field reads 0 as found and nothing writes it, and page 980
   recommends exactly 0 where an overrun is to be treated as an error, which is
   this project's position. **So the as-found value is already the recommended
   one.**

   What the item was really holding open is the transfer-engine back end's
   behaviour, and that is sharper than a field. On this converter an overrun does
   not lose one sample: page 980 says the requests stop and stay stopped until
   software clears the flag, which `acq_timer.c` had already derived from the
   register semantics on Saturday 17 October 2026. So an overrun on the
   transfer-engine build ends the stream, the block counter stops advancing, and
   the console shows a run that went quiet with no line saying why. The timer build
   enables the overrun interrupt and counts and clears the flag in its handler.
   **The open question is whether the transfer-engine build adopts that pattern
   whole**, which would make `acq_conv_overruns` return a count instead of -1.

   And one question is left for the first run rather than guessed: clearing the
   flag unblocks the requests, and nothing read so far says what then holds between
   the stream's current target, its remaining `NDTR` and the samples already
   placed.

7. **`CIRC` is answered and needs nothing**, Tuesday 20 October 2026. RM0455
   Rev 13 section 15.3.11 on page 605 says double-buffer mode enables circular
   behaviour automatically and that the bit is not relevant; the bit's own
   description on page 622 says the hardware forces it to 1 once the stream is
   enabled. The back end leaves it unwritten, which was copied from ST's sequence
   and is now correct for a reason. **It also comes off the first run's suspect
   list**: a stream that delivers two halves and stops cannot be explained by that
   bit.

### The converter's own overrun, closed Thursday 8 October 2026

This was item 2 on the list above the day before, and it is worth recording why it
mattered rather than only that it is done.

**The sampling capture reported `overruns 0` for 390 blocks and that number could
not have been anything else.** `OVRIE` was not enabled and `OVR` was not read, so
nothing was watching the one counter the application's own cannot cover. The gap
was named in three places rather than left for a reader to find, which is the right
treatment for a hole and is not the same as not having one.

**Two numbers now, because they answer different questions.** `overruns` counts
blocks the **application** never collected, which is `main` being too slow.
`convovr` counts conversions the **converter** could not deliver because the
previous result was still in its data register, which is the interrupt being too
slow or not arriving. Either can be zero while the other is not, and a rising
`convovr` with `overruns` at zero would mean the handler is losing samples the
application never hears about.

**Enabling the interrupt was half of it and the halves ship together.** With
`OVRMOD` clear the data register is preserved and the new conversion is discarded,
and `OVR` stays set, so an overrun does not cost one sample: the converter keeps
discarding until the flag is cleared. The handler counts `OVR` and clears it, which
makes the count real and lets the stream recover. Enabling a line whose flag nobody
clears would have held the part in the handler, a worse failure than the one being
closed, so the two changes are deliberately in one commit.

**The marker does not pulse on an overrun-only entry**, which is a one line
decision with the whole measurement behind it. The marker must carry one edge per
**conversion** and not one per interrupt, or the witness would measure this
handler's entries rather than the sampling instants.

**And the other two back ends return -1 rather than 0.** The interface makes the
distinction impossible to lose: 0 means the flag was watched throughout and never
set, -1 means nobody looked. `main.c` says which before the first report line, so
the column cannot be misread. The `dma` build is where -1 is least comfortable,
because a transfer engine reading the data register is exactly the arrangement in
which `OVR` is the primary failure mode rather than a remote one: it is the build
that most needs the flag and the one least entitled to report it.

## Factoring the converter bring-up, scoped Thursday 8 October 2026 and done Friday 9 October 2026

**The scope below is kept as it was written**, because the measurements in it were
the point and because the verification criterion it fixed is what the move was then
held to. The outcome is in the section after it.

The other two back ends cannot reach the converter without this sequence, and there
are two ways to give it to them. **Copying it is the wrong one**, and this
repository has already paid for that lesson elsewhere: three copies of a 333 line
sourced register sequence drift, and the drift does not show up in `git status`.

**The scope, measured rather than estimated.**

| what | where | note |
|---|---|---|
| `adc_bring_up` | `acq_timer.c` lines 364 to 696, 333 lines | all of it converter-generic except the trigger step |
| `report`, `delay_us`, `apb1_divisor` | lines 274 to 363, about 90 lines | all three back ends need the same route to HCLK and the same step reporting |
| the timer-specific remainder | about 20 lines | `EXTSEL` 13 and `EXTEN` 1, plus the line naming TIM6 TRGO |
| the build | `CMakeLists.txt` `SOURCES` | **one entry covers all three targets**, because the `foreach(BACKEND systick timer dma)` loop shares the list |

**One prerequisite is done, and it was not on this list.** Reading `acq_systick.c`
before copying the bring-up into it found that the interface's refusal codes were
never shared: they lived inside `acq_timer.c`, the other two back ends improvised,
and **-3 meant the APB1 prescaler in one and an unconfigured transfer engine in
another.** They now live in `acq_errors.h` with a code per cause. Nothing can be
shared between back ends until the thing they all return is shared.

So the move is about 423 lines into a new `adc_bringup.c` and `adc_bringup.h`,
parameterised on the trigger: `EXTSEL` and `EXTEN` plus the name to print.
`sampling-systick` passes a software trigger, since its tick starts each conversion
in software; `sampling-dma` passes the same TIM6 TRGO the timer build does.

**The verification criterion, published before the work rather than after.** A pure
move changes no register write and no format string, so **the timer build's console
output must be byte-identical before and after, line for line.** Anything else means
the move was not pure, and the difference names the defect. The step reports make
this checkable by eye rather than by hope, which is what they were for.

**Do it in two commits and not one.** First the move with the timer build only, so
the byte-identical criterion is clean and the other two back ends are untouched.
Then `sampling-systick` calling into it, which is a new claim and deserves its own
capture. A single commit doing both would leave a failure ambiguous between the move
and the new back end.

**And it wants the board free.** The criterion needs a build and a flash, and the
board is shared with another project, so a session that cannot flash cannot finish
this safely. Scoping it is the part that did not need the board, and that part is
done.

## Vendor code

The device header and the startup file come from the vendor pack and are **not
vendored into this repository**, because that pack's licence differs file by
file and a project that pasted it in would be redistributing terms it had not
read. Point CMake at wherever it was unpacked:

    cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
      -DCMSIS_DEVICE_DIR=<path> -DSTARTUP_FILE=<path>

No generated code appears here as though it had been reviewed.
