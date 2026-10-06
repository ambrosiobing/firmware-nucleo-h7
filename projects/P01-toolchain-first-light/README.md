# P01: the toolchain, first light, and printf over the ST-LINK

Status: c=board cpp=host python=host rust=host

The project every other one here depends on. It owns the cross toolchain file,
the linker script, the vector table, the reset handler, the clock configuration
and the console, so nothing else has to own any of them.

**State: runs on the board, since Friday 2 October 2026.**

What is measured and what it rests on:

| | |
| --- | --- |
| LD1 green on PB0 | blinks, 499.7 ms per cycle |
| `printf` over COM13 | 115200 baud, divider 556 |
| User button on PC13 | input with a pull-down, active high |
| Core, AHB, APB1 clocks | 64 MHz at reset, decoded from RCC at startup rather than hardcoded |
| The 280 MHz tree | reached Sunday 4 October 2026 by `p01-pll280`: the configuration is at the target, AHB and APB1 at half the core, every one of eight steps reading back what it wrote |
| The core clock, **measured fourteen times** | **279 672 822**, **279 435 368**, **279 714 764**, **279 634 208**, **279 624 968**, **279 541 148**, **279 692 180**, **279 424 340**, **279 522 728**, **279 348 956**, **279 486 828**, **279 411 320**, **279 551 130** and **279 352 464** Hz against this board's 32.768 kHz crystal, 1168, 2017, 1019, 1306, 1339, 1639, 1099, 2056, 1705, 2325, 1833, 2102, 1603 and 2313 parts per million below the 280 MHz nominal. Always low, never the same, spanning 1306 parts per million with no trend, so the sign reproduces and the digits do not. That spread was 998 through seven readings, 1037 through eight, and 1306 through ten, each widening withdrawing a claim this page had just made, and it has now HELD at 1306 through eleven, twelve, thirteen and fourteen. Four holds after three widenings, which is a different state from the one this page described all along and is not the same as settled. The cause is the debugger's clock output, which the fourteen runs put at 7 990 652, 7 983 868, 7 991 850, 7 989 549, 7 989 285, 7 986 890, 7 991 205, 7 983 553, 7 986 364, 7 981 399, 7 985 338, 7 983 181, 7 987 175 and 7 981 499 Hz. Five further runs measured the reset clock only, because in four a second RESET press truncated the report before its core figure printed and in the fifth the capture window was too short |
| The reset clock, **measured nineteen times** | **64 194 318**, **64 186 657**, **64 180 090**, **64 191 988**, **64 202 948**, **64 189 988**, **64 192 428**, **64 194 888**, **64 190 072**, **64 199 102**, **64 200 306**, **64 188 825**, **64 203 931**, **64 205 341**, **64 168 296**, **64 177 416**, **64 197 336**, **64 181 938** and **64 189 764** Hz against the same crystal, spanning 579 parts per million where the PLL spans 1306. Two explanations have been offered for that spread in this repository and the next reading refuted each one, drift by run 4 and temperature by run 5, so no third is offered. The spread held at 357 parts per million through five readings, seven, nine, ten and twelve, runs 13 and 14 widened it to 395, and run 15 came in 11 794 Hz below the previous lowest and widened it to 579. Runs 16 to 19 then all landed inside. That is five widenings and four consecutive holds. What stands is a bound and not a cause, and it is the current bound rather than a settled one: this oscillator reproduces to 579 parts per million on this bench and no better |
| Oscillator | 64.17 to 64.18 MHz, six reductions, two instruments, spread 0.031 per cent |
| Delay loop | 9.00 cycles per iteration at 64 MHz and 8.96 at 280 MHz, the same binary, measured against `DWT_CYCCNT` every boot |
| A 100 ms request | lands within 20 parts per million, checked by the part itself |

**The 280 MHz tree was reached on Sunday 4 October 2026**, by a second image,
`p01-pll280`, and it took five runs on the board and four distinct causes to get
there. `board_clock_status()` now reports `BOARD_OK` on that image and
`BOARD_CLOCK_AT_RESET_SPEED` on `p01-first-light`, which is the honest answer in
both cases: the two images run at different clocks on purpose, because every
cycle figure in this volume was taken at 64 MHz and one image that sometimes
raised the clock would invalidate them all while changing no line of their code.

## What 280 MHz rests on, and what it does not

**It is derived, not measured.** Eight register writes each read back the value
intended, and the frequency is decoded from those registers rather than asserted:
an 8 MHz board fact divided by 4, times 280, divided by 2. Every one of those
reads could be right while the clock is something else.

**The strongest external check available is the console, and it is better than it
looks.** The baud divider is recomputed from the new APB1 frequency of 140 MHz,
and the host's own serial port is clocked by the host. Output that stays clean
over this much text bounds APB1 to roughly two percent of 140 MHz, and therefore
the core to roughly two percent of 280 MHz, because the two differ only by
prescalers that were read back. That is not a measurement of 280 MHz, but it is a
real external bound and it rules out far more than a factor error.

**What no check here can do** is tell 280 MHz from 279, which is 0.36 per cent and
is exactly the error one wrong digit in the PLL's N field would produce. That
needs an instrument that does not share this clock. `freqcount.c` is meant to be
it and still refuses, because the timer registers it needs are not confirmed.

**The delay loop is not independent evidence**, and it is worth saying so because
it looks like it is. It measures itself against `DWT_CYCCNT`, which counts core
clock cycles, so its iterations per millisecond are computed from the frequency
under test. What it does show is that the same binary costs 9.00 cycles per
iteration at 64 MHz with three flash wait states and 8.96 at 280 MHz with six,
which says the loop's cost barely moved and is a fact about the flash and the
pipeline rather than about the clock.

## The core clock, measured nineteen times, and five claims withdrawn

**This is the first frequency in this volume that is measured rather than
derived**, it does not agree with the nominal, and it does not agree with itself.
Nineteen runs on the same board, each gated by this board's own 32.768 kHz crystal
over one second. Runs 1 to 3 are Sunday 4 October 2026, minutes to hours apart.
Run 4 is Monday 5 October 2026 after about eight hours with the board away, so
cold. Runs 5 to 19 are Tuesday 6 October 2026: run 5 after about fifteen minutes
at 280 MHz, so warm; runs 6 and 7 after five minutes unpowered, so cold; and runs
8 to 14 with the thermal state not written down, which is recorded as a blank
rather than guessed at.

**Runs 12 to 19 are the first with the jumper fitted**, so they are also the
first taken while LPTIM1 was actually counting about a megahertz; runs 15 onward
are the first with the counter's two defects fixed; and runs 18 and 19 the first
with the waiting loop's pass rate measured. Those are changes in the conditions
and they are named here because they are real, not because anything is attributed
to them.

**Runs 8 and 9 also come from a different image**, and the record says so: from
Tuesday 6 October 2026 `p01-pll280` also runs TIM1 channel 3 at one megahertz.
Nothing in the measurement path touches TIM1 and both readings land inside the
existing span, but the binary is not the same one that produced runs 1 to 7:

| | the core | against 280 MHz | the internal oscillator | against 64 MHz | the implied input |
|---|---|---|---|---|---|
| run 1 | **279 672 822 Hz** | 1168 ppm low | 64 194 318 Hz | 3036 ppm high | 7 990 652 Hz |
| run 2 | **279 435 368 Hz** | 2017 ppm low | 64 186 657 Hz | 2917 ppm high | 7 983 868 Hz |
| run 3 | **279 714 764 Hz** | 1019 ppm low | 64 180 090 Hz | 2814 ppm high | 7 991 850 Hz |
| run 4 | **279 634 208 Hz** (cold) | 1306 ppm low | 64 191 988 Hz | 3000 ppm high | 7 989 549 Hz |
| run 5 | **279 624 968 Hz** (warm) | 1339 ppm low | 64 202 948 Hz | 3171 ppm high | 7 989 285 Hz |
| run 6 | not printed (cold) | | 64 189 988 Hz | 2969 ppm high | |
| run 7 | **279 541 148 Hz** (cold) | 1639 ppm low | 64 192 428 Hz | 3007 ppm high | 7 986 890 Hz |
| run 8 | not printed | | 64 194 888 Hz | 3045 ppm high | |
| run 9 | **279 692 180 Hz** | 1099 ppm low | 64 190 072 Hz | 2970 ppm high | 7 991 205 Hz |
| run 10 | **279 424 340 Hz** | 2056 ppm low | 64 199 102 Hz | 3111 ppm high | 7 983 553 Hz |
| run 11 | **279 522 728 Hz** | 1705 ppm low | 64 200 306 Hz | 3130 ppm high | 7 986 364 Hz |
| run 12 | **279 348 956 Hz** (wire in) | 2325 ppm low | 64 188 825 Hz | 2950 ppm high | 7 981 399 Hz |
| run 13 | not printed (wire in) | | 64 203 931 Hz | 3186 ppm high | |
| run 14 | **279 486 828 Hz** (wire in) | 1833 ppm low | 64 205 341 Hz | 3208 ppm high | 7 985 338 Hz |
| run 15 | not printed (counter fixed) | | 64 168 296 Hz | 2630 ppm high | |
| run 16 | **279 411 320 Hz** (counter fixed) | 2102 ppm low | 64 177 416 Hz | 2772 ppm high | 7 983 181 Hz |
| run 17 | **279 551 130 Hz** | 1603 ppm low | 64 197 336 Hz | 3083 ppm high | 7 987 175 Hz |
| run 18 | not printed (loop measured) | | 64 181 938 Hz | 2843 ppm high | |
| run 19 | **279 352 464 Hz** (loop measured) | 2313 ppm low | 64 189 764 Hz | 2965 ppm high | 7 981 499 Hz |

**Runs 6 and 8 are half readings, and the reason belongs in the record.** Neither
core figure printed, because the black RESET button was pressed a second time
while the first report was still going out of the port, which cut it off mid
sentence. It happened on two separate captures, so it is a property of the
arrangement and not a slip: the report takes about ten seconds, two one-second
gates plus the printing, and a second press inside that window loses the second
half. The internal figure survives because the image reports it before the clock
is raised. So runs 6 and 7 are seconds apart and closer to one sample than two,
and the same is true of 8 and 9.

**The core is scatter and not drift**, and the first two runs looked like drift.
The readings span 998 parts per million with run 2 lowest, run 3 highest and runs
4 and 5 in the middle, so there is no trend to extrapolate and no latest value to
prefer. Five runs have not widened that spread at all, which is the one thing
about this quantity that has held.

**And the internal oscillator has now refuted two explanations in a row**, both of
them written on this page, both of them read off the data that existed at the
time. This is the useful part of the section, so it is kept rather than tidied
away.

**The first was drift.** After three runs this page said the internal reading fell
monotonically, 119 then 102 parts per million, and called it a warming part. Run 4
came in 185 parts per million **higher** than run 3. Refuted.

**The second was temperature**, and it was written as a prediction precisely so
that it could be refuted. Runs 1 and 4 were both cold and both high; runs 2 and 3
were taken warm and were lower; so a reading taken after the board had been warm
for a while should fall again toward 64 180 000. Run 5 is that reading, taken warm
on purpose. It is 64 202 948 Hz: **22 948 Hz above the figure the prediction
named, 10 960 Hz above the cold run 4, and the highest of all five.** Refuted.

**Runs 6 and 7 were taken to close the question, and they closed it.** With the
temperature direction refuted, what remained was whether cold and warm differ at
all or whether this is simply scatter. Two cold readings after five minutes
unpowered both land **inside** the previous five, 64 189 988 and 64 192 428
against a span that already ran from 64 180 090 to 64 202 948, and the spread
over seven readings is the same 357 parts per million it was over five, to the
hertz.

**The two spreads have now behaved differently, and the core's widened.** That
corrects what this page said a few hours earlier on Tuesday 6 October 2026, which
was that both bounds had survived four later observations. The internal one has.
The core's has not.

The core read 998 parts per million across seven printed figures, and then run 10
came in at **279 424 340 Hz**: 2056 parts per million below nominal and 11 028 Hz
below the previous lowest reading. So the core spread became **1037 parts per
million**, and the claim that it had stopped moving lasted about an hour.

**And then the internal one widened too, after holding six times.** It read 357
parts per million across five readings, 357 across seven, 357 across nine, 357
across ten and 357 across twelve, to the hertz each time. Runs 13 and 14 then read
64 203 931 and 64 205 341, both above the 64 202 948 that had been the ceiling
since run 5. Run 15 then read 64 168 296, which is 11 794 Hz below the previous
lowest of all fourteen, and widened the internal spread to **579 parts per
million**. Runs 16 to 19 then all landed inside it, so 579 across nineteen is
where it stands, and the core **held at 1306** across twelve, thirteen and
fourteen as runs 16, 17 and 19 each landed inside its existing span.

**Four consecutive holds is a new state for this board and it is not settlement.**
Every bound named on this page before now was widened by the reading after it. The
internal 357 also held six times, through five readings, seven, nine, ten and
twelve, before runs 13 and 14 removed it, and a sentence in
`c/board/stm32h7a3_regs.h` that said 357 had survived every test put to it is
still there with its correction beside it, because what that kind of confidence
is worth is part of the record.

**That is the fifth claim about this quantity withdrawn in two days**, after
drift, after temperature, after the core bound, and after the internal bound's
395. The shape is the same every
time: a pattern read off the readings in hand, stated, and removed by the next
reading. Nothing about this quantity has survived being named except that it
scatters, and the two spreads are the current bound rather than a settled one.

**So no third mechanism is offered.** Two were proposed from the pattern in hand
and the next observation removed each one. What survives is a bound and not a
cause: across nineteen readings this part's internal oscillator reproduces only
to **579 parts per million** on this bench, and nothing here may quote it, or any
figure derived from it, to better than that.

**There is a third pattern in the data, and this page names it without claiming
it.** The four cold readings, runs 1, 4, 6 and 7, span 68 parts per million; the
three warm ones, runs 2, 3 and 5, span the whole 357. Read quickly, that says the
warm readings are the unstable ones.

It is not claimed, and the four reasons are the point. Runs 6 and 7 are seconds
apart and amount to about one sample, so the cold set has three independent
members rather than four. The word warm means a different duration in each of
runs 2, 3 and 5, because only run 5 was taken with the elapsed time written down.
Runs 8 and 9 carry no thermal state at all, so the two newest readings cannot be
sorted into either group. And this quantity has already offered two patterns that
looked at least this convincing, and the next observation removed both.

**What would test it**, so nobody has to invent the design later: five or more
readings in each state, with warm defined as a stated number of minutes at
280 MHz and recorded per reading, then the two spreads compared. Until that
exists, the bound above is the whole of what this board supports.

That is worth more than a mechanism would have been. A cause stated from four
points and believed would have been carried into every later chapter; a bound
measured from five is what the next measurement actually needs.

**The instrument is still not what is scattering**, which is what makes the
spread evidence rather than noise. Both oscillators are measured in the same run
through the same gate. `RTC_PRER` reads `007F00FF`, so `PREDIV_A` is 127 and the
sub-second tick is 32768/128 = 256 Hz exactly; 256 of those ticks is one second
exactly, and a single core cycle is 0.0036 parts per million of it. The internal
readings span 357 parts per million across all five runs, a factor of 2.8 smaller
than the PLL's.

**That factor was 4.5 before run 5 and this page is not going to quietly keep the
old number.** The ratio argument is weaker than it was, and if the internal spread
keeps growing while the core's does not, it stops carrying any weight at all.

**What carries the argument instead** is the run-to-run relative movement, which no
widening of either spread explains away. Between each pair of runs the external
clock moved against the internal one by **-730, then +1102, then -473, then
-204** parts per million. No drift of a shared reference can produce a figure that
changes sign twice, so the two oscillators move independently and the larger
movement belongs to the external one.

**So the claim is stronger than after one run, not weaker.** One reading could
only infer that the 8 MHz is not 8 MHz. Four show it is **unstable at the
part-per-thousand level**, which no crystal is, and that says what can be quoted:
nothing derived from this clock is good to better than a part in a thousand
however carefully it is measured.

**There is no constant for any of this, and that is deliberate.** For part of
Sunday 4 October 2026 `stm32h7a3_regs.h` carried `CORE_HZ_MEASURED` as nine
digits, then `CORE_HZ_MEASURED_1` and `_2` when a second run disagreed. The third
run made the shape obviously wrong and the fourth would have demanded a `_4`.
Nothing computes from these figures, so they live in a table beside
`CORE_HZ_TARGET`, which stays 280000000, as `HSE_HZ_BYPASS` stays 8000000 and
`HSI_HZ_NOMINAL` stays 64000000. Every run strengthens that reasoning:
substituting a measured figure would have fitted the code to one sample, and the
sample has now moved four times.

**The crystal was itself cross-checked and holds up in all five runs.** The reset
clock is a different oscillator, and two other instruments had measured it at
64.17 to 64.18 MHz across six reductions against a host PC. The crystal puts it
at 64 194 318, 64 186 657, 64 180 090, 64 191 988 and 64 202 948, all inside or
just above that range. A crystal fast enough to explain the PLL readings would
have put the reset clock near 64 100 000, which is excluded by a factor of three
every time.

**The consequence, with its sign, because the two clocks are wrong in opposite
directions.** An earlier version of this section said "0.117 per cent high", which
is true of the reported frequency at one of the two clocks and ambiguous about
everything else. The table below is the precise form for run 1; the other four
differ in magnitude and not in sign, and the board prints all three quantities
for whichever run it is having:

| | the reset clock | the 280 MHz setting |
|---|---|---|
| `board_core_hz()` reports | 64 000 000 Hz | 280 000 000 Hz |
| the truth, run 1 | 64 194 318 Hz | 279 672 822 Hz |
| so the reported **frequency** is | 3027 ppm **low** | 1170 ppm **high** |
| a measured **duration** comes out | 3036 ppm **long** | 1168 ppm **short** |
| a requested **delay** is delivered | 3027 ppm **short** | 1170 ppm **long** |

Nothing in this volume quotes a time to better than a part in a thousand, so
nothing already published is wrong. What the table is for is the next figure that
wants to be, and for the fact that a figure taken at 64 MHz and the same figure
taken at 280 MHz carry errors of opposite sign, which would otherwise look like a
real effect of the clock change.

**And the uncertainty is a few hundred parts per million, not a few tens.** The
datasheet expectation for a crystal of this kind is tens, and earlier comments in
this repository said so as though it had been shown. It has not. What has been
shown is that two references agree to a few hundred, that this bench cannot say
which of them is the better one, and that the quantity being measured moves by a
part in a thousand between runs of the same image.

## The frequency counter, on the board for the first time

`p01-pll280` links `c/instr/freqcount.c` since Monday 5 October 2026 and reports
on it twice, before the clock is raised and after. Nothing is connected to PD12,
so this run was never going to show that the counter counts. What it was for is
three claims that no amount of host work could settle, and all three held.

| | |
|---|---|
| **the console survived the pin helper moving** | `board_pin_alternate` moved out of `uart.c` into `board.c` in the same commit, and it is what configures the console's own PD8 and PD9. The banner printing at all is the test, and it printed |
| **LPTIM1 is where it was derived to be** | `freqcount_init` returned 0 both times, so LPTIM1 accepted the configuration and read back an autoreload of `0xFFFF`. That address, `0x40002400`, was derived from USART3's absolute address rather than read anywhere, and a wrong base fails exactly there |
| **the ceiling tracks the clock tree** | 64 000 000 Hz on the reset clock and 140 000 000 Hz at the 280 MHz setting, which is the LPTIM1 kernel clock and therefore APB1. A second independent check on the same decode the rest of the image is about |

**What it did not show, stated plainly.** Both measurements returned 0
millihertz. That is the correct frequency of an idle line held high by its
pull-up, and it is also what a refusal returns, so the counting path was
unverified through seven runs.

**Since Tuesday 6 October 2026 the image carries the source as well**, so what
is missing is a wire and a flash. `c/instr/pwmsrc.c` drives TIM1 channel 3 on
PE13 at one megahertz, `p01-pll280` starts it before each counter reading, and
the report now prints both numbers and the distance between them. Three readings
are possible and the report says which one it got:

| the report says | what it means |
|---|---|
| `measured 0 millihertz while the source produces 1000000000` | the source runs and no wire carries it. This is the reading to expect until PE13, Arduino D3, is joined to PD12, Arduino D29 |
| the two figures a few parts per million apart | the counting path works, and the crystal gate and the PLL agree to that figure through paths sharing no component but the crystal |
| `pwmsrc_start apb2-prescaler-undecoded` | `CDPPRE2` is not at divide by one, so the timer clock depends on the half of the `TIMPRE` rule that lives in RM0455 and has not been read. A refusal rather than a guess |

**It got the first of those on Tuesday 6 October 2026, and three things that no
host could settle held with it.**

| | |
|---|---|
| **`RCC_APB2ENR` is at `0x150`, not the `0xF0` ST's comment says** | This header took `0x150` from walking `RCC_TypeDef` by declaration order rather than reading its trailing comments, which are the STM32H743's from `RSR` onward. If the address were wrong, `TIM1EN` would be written elsewhere, TIM1 would stay unclocked, and its registers would not hold writes. `pwmsrc_start` reads `PSC`, `ARR`, `CCR3`, `CCMR2` and `CCER` back before enabling the output and returned `ok` at both clocks, so the enable reached TIM1. The same argument confirms `TIM1_BASE` at `0x40010000` |
| **the APB2 decode** | the source reported its timer clock as 64 000 000 Hz on the reset clock and 140 000 000 Hz after the raise, both with `CDPPRE2` at divide by one, which is the field added to the decode the same day |
| **the arithmetic, against the hardware rather than against a table** | `PSC 0 ARR 63 CCR3 32` at 64 MHz and `PSC 0 ARR 139 CCR3 70` at 140 MHz, both reported as producing exactly 1 000 000 000 millihertz, 0 ppm from the target. Those are the fields `python/tests/test_pwmmath.py` predicts from the definition, now written into a real timer that accepted them |

**And on Tuesday 6 October 2026 the wire went in and the counter counted.** One
jumper, CN10 pin 10 to CN10 pin 21, and run 12 reported 87 903 millihertz on the
reset clock and 82 019 at the 280 MHz setting, where every run before it had
reported 0. Edges arrive on PD12, LPTIM1 counts them, and the number comes from a
real signal. That is the claim this project could not make from Monday 5 October
2026 until that moment, and it is now made.

**The number was also wrong, low by a factor of eleven to twelve**, and the size
of the error named its own cause. `freqcount_measure_mhz` samples the counter
twice, once before the gate and once after, and each sample can add at most one
wrap, because `LPTIM1_ISR`'s `ARRM` is a flag meaning one or more matches since
`ARRMCF` and not a count of them. A gate spanning fifteen matches contributes
one. The reported edges are then 65 536 plus the 16-bit residue whatever the
input, which caps any one-second reading at 131 071 Hz. Both readings sat on that
model and both were under the cap.

**So a falsifier was written before any fix**, because a model that merely fits is
not a model that holds. Twelve ticks of the 256 Hz crystal tick is 46.875 ms, and
any source below 1 398 101 Hz cannot wrap a 16-bit counter inside it, which is
where the two-sample scheme is exact rather than approximate. The refuting band
was 65 000 to 131 000 Hz.

| gate | reading | |
|---|---|---|
| long, 256 ticks | 87 938, 88 101 and 82 550 Hz | the wrap accounting |
| short, 12 ticks | 1 041 579, 1 041 877 and 1 032 768 Hz | not in the refuting band |

**The wrap model holds.** The long gate's shortfall is the wrap accounting and
nothing else, and the fix is now aimed at something confirmed.

**And the same capture exposed a second defect, which is the better find.** The
short gate's parts-per-million figure was supposed to equal the clock offset
printed a few lines above it in the same report, because the source is APB2 over
a whole number and so follows the clock, while the gate is the crystal. It should
have read about +3186 ppm at the reset clock and about **minus** 1833 at 280 MHz.
It read +41 579, +41 877 and +32 768: wrong size, and at 280 MHz wrong sign.

Dividing each reading by the nominal megahertz and multiplying by twelve gives
implied gates of **12.50, 12.50 and 12.39 ticks**, not twelve. Taking the source's
real frequency instead of the nominal, which separates the gate excess from the
clock offset, gives 12.46, 12.46 and 12.42. Either way it is an extra half tick on
average, which is what a uniform wait of nought to one tick looks like over three
draws.

**The structure that produces it is the bracket, not the crystal.**

```c
const uint32_t before = counter_now();
lseref_measure_core_hz(&gate, ticks);   /* waits for its own first edge, then counts ticks */
const uint32_t after = counter_now();
```

`lseref_measure_core_hz` gates the core correctly: it waits for a tick boundary,
takes its first cycle count, counts exactly `ticks` intervals, then takes its
second. The LPTIM samples sit **outside** that gate, so the edge window includes
lseref's setup and its initial boundary wait while the divisor is only `ticks`.
The counter's window is wider than the gate it is divided by.

That predicts an excess of nought to 8.3 per cent at twelve ticks and nought to
0.39 at 256. Observed 3.5 to 3.9 at twelve. It also retro-explains the 0.19 to
0.24 per cent residual left on the wrap-model fit at 256 ticks, which had been put
down to sampling noise. One defect, two symptoms, scaled by the gate.

**Both have one cause, and the fix was written and then confirmed on the board the
same day.** The cause was that `freqcount` bracketed a call it did not control. It
owns the gate loop as of Tuesday 6 October 2026: it opens the tick source itself,
waits one tick so the first sample sits on a boundary, samples, then waits exactly
`ticks` boundaries while polling and clearing `ARRM` between them, and samples
again. The poll runs thousands of times per tick instead of twice per gate, and
both samples now sit inside a window whose length is exactly the ticks it is
divided by.

**The confirmation is not a frequency, it is the implied gate length.** That is the
quantity each defect moves, and it is read off the report without trusting the
nominal megahertz: take the measured rate, divide by the rate the crystal-measured
clock says the source must be, and multiply by the ticks asked for. Twelve were
asked for every time.

| | run 13, 14 and before | run 15 and 16 |
|---|---|---|
| implied gate, 12 ticks asked | 12.39, 12.46, 12.50 | **12.002, 11.999, 12.002** |
| implied gate, 256 ticks asked | not separable from the wrap defect | **256.014, 256.008, 256.000** |
| one second reading, source at a megahertz | 87 938, 88 101, 82 550 Hz | **1 002 684, 1 002 805, 997 897 Hz** |

**Always above twelve is a bias; straddling twelve is noise.** That one sentence is
the result. The wrap cap of 131 071 Hz is gone, and the half tick the edge window
used to carry is gone with it.

**The best reading this instrument has taken** is run 16 at 280 MHz: the one second
gate came back **0.6 parts per million** from the figure the crystal-measured core
requires, which is inside that gate's own one hertz resolution. Two paths to one
quantity, sharing no component but the crystal, agreeing to better than a part in
a million. Nothing else in this volume is measured that well.

**And the short gate was mislabelled on this page, which is worth correcting rather
than quietly fixing.** It was called the sensitive one without saying sensitive to
what. It amplifies a gate-width error about twenty-one fold against the long gate,
which is what it was built for, and it is at the same time the **worst** frequency
reading in the report, because one edge is 21 ppm over 46.875 ms against 1 ppm over
a second. Better detector, worse meter. Those are not in tension and only the
flattering half had been written.

**So the residual is read off the long gate.** Subtracting the clock offset from
each reading leaves the instrument's own disagreement:

| gate | | reset clock | | | | | at 280 MHz | |
|---|---|---|---|---|---|---|---|---|
| run | 15 | 16 | 17 | 18 | 19 | 16 | 17 | 19 |
| 256 ticks | +54 | +33 | +24 | +104 | minus 74 | **minus 0.6** | +6 | **+407** |
| 12 ticks | +165 | minus 105 | +74 | +101 | +171 | +140 | **+3** | +479 |

All in parts per million. The **minus 0.6** and the **+3** are the best the two
gates have done, each inside its own resolution. The **+407** is the one that
matters, and it is dealt with below.

The short gate's three figures **change sign**, so they are scatter and not bias.
Its own quantisation is one edge at each boundary, about 43 ppm, and the rest is the
time one pass of the waiting loop takes at each boundary, which **was not measured**.
That unmeasured figure is the single thing that would turn this scatter from
unexplained into bounded.

**So the loop reported what it already counted, and the threshold was written down
before the number existed.** It increments a poll counter on every pass in order to
bound itself against a tick that never arrives, and that count was being discarded;
`freqcount_last_poll` makes it readable at no cost to the loop. Two boundaries, each
detected to within one pass, give a gate-length uncertainty of two over passes per
tick times ticks. Setting that equal to the observed 140 ppm over twelve ticks gives
about **1190 passes per tick**, so the threshold published in advance was:

| what the board reports | what it would mean |
|---|---|
| near 1200 passes per tick | the loop's own granularity accounts for the scatter, and the residual is bounded |
| near 20 000 | it accounts for about 8 ppm of 140, and something else is in there |

**Runs 18 and 19 answered it: 2298 passes per tick at the reset clock and 3925 at
280 MHz**, with a within-gate spread of four passes in 2298 and three in 3925. That
is near the 1200 end.

| | passes per tick | one pass | uncertainty over 12 ticks | over 256 |
|---|---|---|---|---|
| reset clock | 2298, min 2296, max 2300 | 1699 ns | 72.5 ppm | 3.4 ppm |
| at 280 MHz | 3925, min 3924, max 3927 | 995 ns | 42.5 ppm | 2.0 ppm |

With the edge quantisation's 42.7 ppm, **the two known terms reach 115 ppm of about
138 observed** at the reset clock. The loop's granularity is the dominant term and
the two together account for most of the scatter, leaving a modest remainder rather
than a large one. Neither "explained" nor "unexplained" is the right word for that,
which is why the figures are given instead of a verdict.

**The tight within-gate spread matters separately.** Four passes in 2298 means the
loop runs at a very steady rate, so the uncertainty is the **granularity** of one
pass and not variation in how long a pass takes. Those are different findings and
the minimum and maximum were printed so they could be told apart.

**And the loop is not core bound**, which the two clocks settled for nothing. The
pass time fell by a factor of **1.71** when the core rose by **4.375**. Fitting a
fixed term plus core work to the two points gives about 787 ns of clock-independent
latency and about 58 core cycles of work per pass. Two points fitted to two
parameters is exact by construction and therefore untested: a third clock would test
it and none has run. What the two points establish with no fitting at all is the
ratio, 1.71 against 4.375, which no purely core-bound loop can give.

**The old estimate was right about the half it modelled.** `freqcount.c`'s comment
said "call it fifty core cycles", against 58 measured. It was not wrong about the
core work; it was silent about the fixed latency, which is the larger term at both
clocks. An estimate that models one of two terms is not half right, it is
confidently wrong about the total.

**And the open question has moved, which is the real result.** Run 19 at 280 MHz
returned a long-gate residual of **+407 ppm**. Over a 256-tick gate this
instrument's whole error budget is 3.4 ppm of loop granularity plus 2 ppm of edge
quantisation, which is **5.4 ppm**. A residual eighty times that cannot be the
counter, and the elimination is arithmetic rather than judgement. The two
instruments measure one quantity seconds apart, and the core readings at 280 MHz
across runs 16, 17 and 19 themselves span **710 parts per million**. So the next
measurement is of the clock and not of the counter.

**That measurement is now written, and its threshold is published here before any
board has run it.** It needs no new instrument: it is `lseref_measure_core_hz`, the
one that already produces every measured frequency on this page, run more than once
inside a single boot. Two separations are reported, because they are two questions.

| | separation | what it asks |
|---|---|---|
| immediately after the first | about one second, the gate's own length | does the clock hold still over one gate |
| after the source and both counter gates | three to four seconds | does it hold over the span the counter's residual actually covers |

A clock that holds over one second and moves over four would show as a small figure
for the first and a large one for the second, which is why one measurement would
not have done.

| what the board reports | what it would mean |
|---|---|
| near 400 ppm | the clock moves that much inside one run, and run 19's +407 ppm counter residual is accounted for with nothing further wrong |
| near 1 ppm | the clock holds inside a run, the +407 came from somewhere else entirely, and that is a **new** problem rather than a closed one |
| 10 to 100 ppm | part of the story and not all of it, which leaves the counter's larger residuals open |

**The middle outcome is the least convenient and it is listed rather than left
out.** An experiment whose awkward result has no entry in the table written
beforehand is one that will be read charitably afterwards.

**And it is a ratio, not the core alone.** Both readings are core cycles over a
crystal gate, so a crystal that moved between them would look identical. The
crystal is a 32.768 kHz quartz and the two other oscillators here have already
shown hundreds of parts per million of scatter, so the core is the likelier mover,
but this measurement does not separate them and does not claim to.

No cause is offered until the board answers, because the within-run stability of
this part's clock has never been measured here, which is exactly why it cannot be
blamed yet.

**No cause is offered for the long gate reading +54 and +33 at the reset clock and
minus 0.6 at 280 MHz.** There is one difference worth naming without attributing
anything to it: at the reset clock both instruments descend from the internal
oscillator, whose reproducibility between runs on this bench is 579 parts per
million and whose stability **within** a run has never been measured here, while at
280 MHz the chain runs from the debugger's 8 MHz in bypass. That is one observation
at each clock. This page has withdrawn five explanations of this part's clock
scatter and it offers no sixth.

**An interrupt was considered and rejected on the facts**, not on taste. `ARRM` is
a flag and not a counter, so a handler late by one autoreload loses a match
exactly as two samples did, and at the usable ceiling of 16 777 216 Hz a wrap
takes 3.9 ms against a 3.906 ms tick, which is precisely where two can hide in
one. A poll inside a loop that is already waiting has no such latency and adds no
vector. The interrupt stays the right instrument for something that must run while
the core is not in this loop; this measurement is that loop.

**The arithmetic the fix depends on is checked off the board**, in
`python/tests/test_freqmath.py`, which drives the composition in the shape
`freqcount.c` calls it with nine hand-written cases including the borrow, and
then recomputes every expected value a second way from the definition. Double
entry has found three errors in this repository's hand tables already.

**One megahertz is not an arbitrary target.** It divides both timer clocks
exactly, and with different fields: PSC 0 and ARR 63 at the reset clock's 64 MHz
APB2, PSC 0 and ARR 139 at the raised clock's 140 MHz. Same output from different
registers, which is why the source is started twice rather than left running. If
the counter reads one megahertz at both clocks, the APB2 decode is confirmed as
well as the counting path, because a wrong APB2 figure would turn the same
request into a different frequency.

It also sits well under the counter's 16 777 216 Hz usable maximum, puts 3906
edges in each 256 Hz crystal tick so no count comes near the 16-bit wrap between
polls, and keeps the millihertz figure inside the 32-bit `unsigned long` these
reports print through. That last one is a limit on what can be printed rather
than on what can be measured, and above about 4.29 MHz the two part company,
which is exactly the sort of difference that would otherwise be read as a
result.

**And the obvious way to get one is wrong, which this page proposed before it
was thought through.** ST's sibling example `LPTIM_PWMExternalClock` puts
`LPTIM1_OUT` on PD13 and `LPTIM1_IN1` on PD12, adjacent pins, so one wire
between them looks like a self test. It is not. That example's name says why:
the counter is clocked by IN1 and the compare generates the output, so the
output frequency is derived from the input. Wiring one to the other makes a
feedback loop, and counting a signal generated from the counter under test
establishes nothing at all.

**What replaces it compares two instruments instead of one with itself**, and
since Tuesday 6 October 2026 it is written and in the image rather than only
planned. TIM1 channel 3 on PE13 at alternate function 1, one wire to PD12, and a
PWM frequency chosen well under the 16 777 216 Hz usable maximum. A general purpose timer's
output is derived from its APB clock and therefore from the PLL, so counting it
against the crystal gate compares the PLL to the crystal through a path sharing
nothing with `c/instr/lseref.c`: lseref counts core cycles inside the part with
`DWT_CYCCNT`, while this counts edges arriving on a pin from outside the counter.
Two instruments, one quantity, no component in common but the crystal.

Unlike a self test it can also fail informatively. If the two agree, the crystal
gate and the cycle counter are confirmed against each other and the four core
clock readings above gain a second witness. If they disagree, the size of the
disagreement says which kind: a ratio near a small integer points at a
prescaler or a divider misread, a few hundred parts per million points at the
crystal, and a disagreement that moves between runs points back at the
debugger's 8 MHz, which is already known to move by a part in a thousand.

**The connector positions are sourced from ST, and both pins are on the same
header.** PE13 is **CN10 pin 10** and PD12 is **CN10 pin 21**, so the wire is one
jumper across one connector:

| | from ST, for this board |
|---|---|
| PE13 | `Examples/TIM/TIM_DMA/readme.txt`: "TIM1 CH3 (PE.13) is connected to pin 10 (D3) on CN10 Connector" |
| PD12 | `Examples/LPTIM/LPTIM_PulseCounter/readme.txt`: "Generate pulses on PD12 (pin 21 in CN10 connector)" |
| PD12 again | `Examples/LPTIM/LPTIM_PWMExternalClock/readme.txt`: "Connect a clock signal to PD.12 (connected to pin 21 in CN10 connector)" |

all under `Projects/NUCLEO-H7A3ZI-Q` in `STM32Cube_FW_H7_V1.13.0`, read Tuesday 6
October 2026.

**That also confirmed the Arduino numbering from a third source, and caught ST
disagreeing with itself.** D3 for PE13 had come from Zephyr and from the STM32
Arduino core, which agreed; ST's own readme now says it too. Those readmes give
twelve further port-pin to Arduino-number pairs and the Arduino core agrees with
twelve of the thirteen. The thirteenth is ST against ST: one file says
`CLK Pin: PA5 (CN07.D10)` and another says `PA5 : CN7.D13`. Zephyr and the Arduino
core both say D13, and on an Arduino Uno R3 header D13 is SCK while D10 is the
chip select, so D13 is right and the D10 is a slip. Nothing here depends on PA5,
but it is worth knowing these readmes are not individually authoritative.

**The Arduino name D29 for PD12 is the weaker of the two labels** and the
difference is kept deliberately. CN10 pin 21 comes from ST; D29 comes from the
Arduino core's variant index and no ST file for this board says it. Wiring needs
the pin number, so that is the fact to carry. Neither source is ST, so neither was taken alone. Zephyr's board
`nucleo_h7a3zi_q` maps PE13 to `ARDUINO_HEADER_R3_D3`; the STM32 Arduino core's
variant for this exact part puts `PE_13` at `digitalPin` index 3 and `PD_12` at
index 29, in an array holding only pins the header exposes. The two were written
independently and agree exactly on the one pin both cover, which is what makes
the index for the other worth acting on. Zephyr's page for this board also names
PD8 and PD9 for USART3, PC13 for the button and PB0, PE1 and PB14 for the LEDs,
every one of which this board has already printed, so the source was checked
against this bench on facts it could have got wrong.

What is still unread is a convenience and not a blocker: which of the four
connectors each pin sits on, and its position within it. That is ST's table, in
**UM2408**. This volume said UM2407 in four places until Tuesday 6 October 2026,
and that was wrong in the way the volume is about: UM2407 documents MB1364, the
NUCLEO-H743ZI2, and UM2408 documents MB1363, which is this board. The error is
recorded rather than quietly removed, because it is the book's own trap
committed in the book's own source tree.

## The decode can be wrong in eight ways, and now seven of them can be tested

Until Sunday 4 October 2026 the clock tree decode lived as four static functions
inside `c/board/system.c`, and every one of them dereferenced a memory mapped
register. That made it correct and untestable in one stroke. Nothing on a host
could call it, so the only test it had was to flash the board and read the
banner, and a flash exercises exactly one configuration: whichever one the board
happens to be in.

It passed that test. The measurement above agrees with the decoded 280 MHz to
1168 parts per million and the remainder is traced to the input frequency rather
than to any field. **But the cases that matter most are the refusals**, and they
are the cases the board has no convenient way to produce. There is no comfortable
way to ask this part for a PLL with its fractional term enabled, or a bus
prescaler holding a ratio nobody has sourced, and those are precisely where a
wrong decode returns a plausible frequency instead of a dead board.

So the decode moved to `../../c/clock/clocktree.c`, taking the seven register
words as an argument, and `system.c` now reads the registers and calls it. **One
copy of the field rules, and a host can drive all eight refusals.** The
arithmetic did not move: the same masks, the same order, the same integer
truncations, which is what lets the board measurement go on applying to it.

| | |
|---|---|
| the oracle | [`clock_vectors.json`](clock_vectors.json), 20 decode rows and 8 bias rows, **two of the 17 being this board** |
| what each row carries | the seven register words, the four frequencies or the named refusal, and a `why` saying what the row is for |
| how the answers were obtained | both halves written by hand, then checked against an independent recomputation that refuses to write the file if they disagree: [`gen_clock_vectors.py`](../../python/tools/gen_clock_vectors.py), which nothing in the build or the suite calls |
| what drives it | `python/tests/test_clocktree.py` through the same object file the firmware links |
| proven able to fail | three deliberate mutations on Sunday 4 October 2026, each turning exactly one test red: a register named in the decoder's code, a ninth refusal with no vector, and a vector row deleted |
| what the first compiler found | nothing wrong with the decode, and one test of its own asserting more than the oracle says. The `a-half-part-per-million` rows carry a frequency of 1 and a duration of 0, because one divides by 2000000 and lands on exactly half while the other divides by 2000001 and lands just under. The two quantities never carry the SAME sign; they do not always carry opposite signs, and the assertion now says the weaker true thing |

**Three rows are worth naming.** The first configures the PLL completely for
280 MHz and leaves `SWS` reading HSI, and the answer must still be 64 MHz: an
implementation that reported the PLL it found configured would pass every other
row in the file. The second decodes the 280 MHz registers at the measured
7 990 652 Hz input rather than the nominal, which is why `clocktree_decode` takes
both input frequencies as arguments instead of reading them from the header; it
gives 279 672 820 Hz against a measurement of 279 672 822, and the two hertz are
worth keeping, because this chain multiplies its input by exactly 35 and
279 672 822 is not a multiple of 35. No integer input reproduces the measured
figure, which is a statement about the measurement's own resolution and not about
the decode. The third sets `DIVM1` to 3, where 8 MHz does not divide exactly, and
pins the order of the arithmetic: divide then multiply gives 373 333 240 and
multiply then divide gives 373 333 333. The board's own configuration divides
exactly, so it could never tell those apart.

**And the refusal now says which.** `board_clock_status()` has one error value
and the decode has eight ways to reach it, so a reader looking at a board that
reports `BOARD_ERR_CLOCK_UNCONFIRMED` had eight places to look.
`board_clock_refusal_text()` returns one of nine short tokens, `p01-pll280`
prints it when there is one, and the tokens are the same nine in every language
so the parity comparison is of reasons and not only of numbers.

**And two of the rows are now this board rather than constructions.** Every row
in the file was built by hand until `p01-pll280`'s dump gained `RCC_CDCFGR2`, the
one word the decode reads and the sequence never writes: six of the seven were
being printed, and six of seven is none. With the seventh in the capture of
Sunday 4 October 2026 the file carries the state as found after a RESET press and
the state after the eight-step raise, both with the frequencies the image
reported for them, both agreeing with the independent recomputation.

They are not duplicates of the synthetic rows they resemble. The as-found row is
a stronger version of the `SWS` row above, because the reset state really does
hold a plausible-looking PLL configuration, `DIVP1EN` already set with `DIVM1`
reading 32 and `N` reading 129, while `SWS` says HSI; an implementation reading
the PLL instead of `SWS` would report a frequency this board has never run. And
the configured row carries whole words rather than only the fields the decode
reads, reserved bits and the other PLLs' dividers included, so a mask that was
too wide would pass the synthetic row, where the neighbours are zero, and fail
here, where they are not.

**What is still not here** is a board row taken after a power cycle rather than a
RESET press. The supply selection, the regulator's ready state and `HSEBYP` all
survive a system reset, so the as-found row is the post-RESET state and not the
reset state, and the file says so itself rather than leaving a reader to notice.

## The 32.768 kHz crystal, asked to oscillate for the first time

It had been a settled board fact since Friday 2 October 2026, from two
machine-readable sources, and nothing in this repository had ever switched it on.
On Sunday 4 October 2026 `p01-pll280` did, and it works:

| | |
|---|---|
| after a power cycle | `RCC_BDCR` went `00000000` to `00000003`, so `LSEON` and `LSERDY` are both set, after about **1093 ms** of waiting |
| after the RESET pin | `RCC_BDCR` was already `00000003` and the wait was zero |

**Why it matters more than it looks.** Every frequency in this volume is derived
from an 8 MHz board fact and register fields that were read back, and a read-back
proves the bits rather than their meaning: reading `DIVM1` as 4 does not prove
the field is a plain divisor. This crystal is the only reference on the board that
does not come from the PLL chain, so counting core cycles against it can settle
the interpretation, and a crystal at 32.768 kHz is good to a few tens of parts
per million by its datasheet against the 0.36 per cent that separates 280 MHz
from 279. What this bench has actually demonstrated is weaker and is below: the
crystal and a host PC's clock agree on the internal oscillator to a few hundred
parts per million, and which of the two is the better reference is not something
this bench can show.

**The 1093 ms is a bound rather than a measurement**, and the distinction is the
usual one here. It is counted by `board_delay_ms`, which is calibrated against
the clock under test, so it carries whatever error that clock has. It is still
worth having: it is comfortably inside what the datasheets allow for an
oscillator of that frequency, and it means anything that waits on this crystal
needs a bound in seconds rather than the tens of milliseconds a PLL ready flag
takes.

**Two prerequisites that would each have failed silently.** `RCC_BDCR` ignores
writes while `PWR_CR1`'s `DBP` bit is clear, so the enable would simply not have
happened and the crystal would have looked absent; `DBP` is set first and read
back. And the backup domain survives a system reset, exactly as the supply
selection does, so a zero wait after a RESET press means nothing was asked of the
crystal rather than that it started instantly, which no oscillator of that
frequency does. The record carries `RCC_BDCR` as found before any write so the two
are distinguishable, and the table above is both cases observed rather than
reasoned.

## One thing a reset does not undo

Running `p01-pll280` twice on Sunday 4 October 2026, once after a power cycle and
once after the black RESET button, gave different starting states:

| | after power is removed | after the RESET pin |
|---|---|---|
| `PWR_CR3` | `00000006`, both `LDOEN` and `SMPSEN` set, no supply chosen | `00010004`, `LDOEN` clear, the SMPS still selected |
| `ACTVOSRDY` | 0, the regulator has not settled | 1, already settled |
| `HSEBYP` | 0 | 1, still in bypass |
| supply step | 31 polls to become ready | 0 polls, nothing to do |

**The supply selection, the regulator's ready state and the external clock's
bypass bit all survive a system reset.** Only removing power restores them. Two
consequences worth having written down. A reader comparing the image's dump
against a reset-value column in a manual will find disagreements that are not
faults, which is why the heading says "as found" rather than "at reset". And the
supply step is safely idempotent: writing the selection it already holds is a
no-op that still passes its own read-back gate, which is what ST's own
`HAL_PWREx_ConfigSupply` has a branch for.

This is also why the earlier failures needed a power cycle rather than the RESET
button. A locked supply selection is not cleared by a reset, so a wrong choice
stays wrong until the board loses power.

## The four causes, because the sequence is the lesson

Three of these were avoided by reading ST's headers before writing anything, and
the account is in `c/board/stm32h7a3_regs.h` beside each field. Two were found by
the board.

| | What |
|---|---|
| read, not guessed | the voltage scale encoding, which is **reversed** between this part and the STM32H743: `PWR_REGULATOR_VOLTAGE_SCALE0` is 0b11 here and 0b00 there, in the same file, guarded by device |
| read, not guessed | `DIVM1` holds the value and `N1`, `P1`, `Q1`, `R1` hold the value minus one, which is not a symmetry and is settled by how the HAL reads them back |
| read, not guessed | ST divides the same 280 MHz differently from the note this repository carried, with a 2 MHz PLL input rather than 4 MHz, and pairs it with the input range that contains 2 MHz |
| **found by the board** | voltage scale 0 is reachable only from scale 1. Writing it directly from the reset scale read back correctly and simply never became ready |
| **found by the board** | a supply has to be selected before any voltage scaling works at all. At reset both `SMPSEN` and `LDOEN` are set, which is the absence of a choice, and `ACTVOSRDY` is clear from reset to say so |
| **found by the board, destructively** | this board's core is supplied through the SMPS. Selecting the LDO stopped the part, and a power cycle recovered it completely |

The last of those was my error and the account is at `PWR_CR3` in
`stm32h7a3_regs.h`. I had one conditional branch in ST's source that selected the
LDO and an argument that the LDO is on the die and therefore safe. The argument
is true about the die and says nothing about the board. ST sets the supply in the
project configuration, where every `.cproject` for this board names
`USE_PWR_DIRECT_SMPS_SUPPLY`, and reading that would have taken a minute.

**Nothing spins forever in the sequence**, and that is why four of the five runs
produced a readable diagnosis instead of a dead board. ST's own example waits on
`VOSRDY` with `while(!flag){}`, which on this part never terminates from the reset
state. Every wait in `clock280.c` is bounded and running out is a reported
failure with the register's value attached.

It is built on the **win11 skyhorizon demo laptop**, where STM32CubeIDE 2.2.0
supplies `arm-none-eabi-gcc` 14.3.1, `cmake` and `ninja`, none of them on PATH;
`Use-CubeIDEToolchain.ps1` finds them. It
cannot be built on win11 aquamarine, which has no cross toolchain at all, and
flashing needs no tool anywhere: the probe presents a disk and a `.bin` copied
onto it is programmed.

## What first light means here, and why it can work before anything is confirmed

The part starts on its internal oscillator, and the three LEDs are settled board
facts: **LD1 green on PB0, LD2 yellow on PE1, LD3 red on PB14**, with the user
button on **PC13**, from two independent machine-readable sources that name this
exact board and agree.

So an LED can be made to blink without reading a single register address out of
RM0455. That is the design of this project: **a blinking LED is the first thing
that works, not the last.** It proves the toolchain, the linker script, the
vector table and the reset handler all at once, and it proves them before any of
the harder questions are answered.

Holding the button lights all three LEDs. That is the whole self-test: if three
LEDs light, then four settled pin facts and the GPIO configuration are all
correct together, and anything that goes wrong afterwards is something else.

## What it refuses, and what each refusal costs

Three of the four refusals this project started with have been lifted, each by
reading an authority rather than by guessing. What remains is one row, and it is
the row that matters most:

| Refused | Why | What it needs |
|---|---|---|
| 280 MHz core clock | the PLL fields, the flash access latency and the voltage scaling are all RM0455's and none has been read | RM0455, the clock tree and power chapters |

| Lifted, and by what | When |
|---|---|
| Every GPIO and RCC access: the peripheral base addresses came from ST's own CMSIS device header for this die, named in `BOARD_REGS_CONFIRMED` | Friday 2 October 2026 |
| The console, and therefore printf: the probe's virtual serial port pins were settled and recorded in `BOARD_CONSOLE_PINS_CONFIRMED`, and the console reaches COM13 at 115200 | Friday 2 October 2026 |
| Claims about time: `board_core_hz()` reports the clock decoded from RCC at startup, and the delay loop calibrates itself against `DWT_CYCCNT` on every boot | Friday 2 October 2026 |

Each refusal is a value returned, not a comment. `board_clock_status()`,
`board_console_status()` and `board_core_hz()` are how a project finds out, and
`main.c` prints all three and signals them on the LEDs as well, because a board
with no working console that merely sits there tells a reader nothing. The one
remaining refusal is still reported that way: `board_clock_status()` returns
`BOARD_CLOCK_AT_RESET_SPEED`, which is why every cycle figure in this volume is
a figure at 64 MHz and says so.

**Why refuse rather than use a plausible value.** The three clock values each get
the part wrong in a different way. Too little flash latency executes garbage once
the clock rises. Wrong voltage scaling works at room temperature and fails warm.
The wrong PLL divider is the worst of the three: the part runs at a plausible
wrong speed, everything appears to work, and every measured interval in every
later project is wrong by a constant factor. A clean square wave at the wrong
rate is indistinguishable from a right one without an external witness, which is
P06's whole subject.

## What the register addresses rest on

`c/board/stm32h7a3_regs.h` is the one file that carries an address, and it says
at the top what each one rests on. Both confirmation macros are now defined:
`BOARD_REGS_CONFIRMED` names ST's CMSIS device header for this die, read on
Friday 2 October 2026, and `BOARD_CONSOLE_PINS_CONFIRMED` names the probe's
virtual serial port pins. One placeholder remains, compiled out, and the 280 MHz
sequence is the authority still unread.

The header also carries the clearest evidence of this volume's central trap. On
this die the peripheral bases are offsets from `SRD_AHB4PERIPH_BASE` and
`CD_APB1PERIPH_BASE`: two power domains, CD and SRD. The STM32H743 that almost
every STM32H7 tutorial is written against has three, D1, D2 and D3, and the
correspondence is not a rename. Code copied from an H743 project does not compute
a wrong address here, it fails to resolve the symbol at all, which is the kindest
way this trap can present.

**Use RM0455 and not RM0433.** RM0433 documents the STM32H743, which is the
member of this family the internet is about. Its clock tree, its power and
regulator configuration and its memory map all differ. Code copied from material
that says "STM32H7" does not run slowly here; it produces a board that does not
boot.

## Layout

Shared, at the repository root, because every project links or uses it:

    cmake/arm-none-eabi.cmake     the toolchain file, used by every project
    CMakeLists.txt                the firmware build
    c/ld/stm32h7a3zi.ld           the linker script, lengths from this part
    c/board/startup.c             vector table and reset handler, in C
    c/board/system.c              reads the seven registers and calls the decode
    c/board/clock280.c            the eight gated steps to 280 MHz
    c/board/board.c               LEDs, button, board_init
    c/board/uart.c                the console, polled
    c/board/cycles.c              DWT_CYCCNT, the only cycle counter in the volume
    c/board/icache.c              the instruction cache
    c/board/retarget.c            printf, and the cost of that decision
    c/board/board.h               what this project provides to every other
    c/board/stm32h7a3_regs.h      the registers, and what each one rests on
    c/clock/clocktree.h           the decode's interface, and why it is not in
                                  c/board: it touches no register
    c/clock/clocktree.c           register values in, four frequencies or a named
                                  refusal out, callable by a host compiler
    c/instr/lseref.h              the crystal reference's interface
    c/instr/lseref.c              starts the crystal and counts core cycles
                                  against its sub-second tick
    c/instr/freqcount.h           the frequency counter's design, and the two
                                  times it changed
    c/instr/freqcount.c           LPTIM1 counting edges on PD12, gated by the
                                  crystal
    c/instr/freqmath.h            and its arithmetic, which reads no register
    c/instr/freqmath.c            so a host can test the divide
    c/instr/pwmsrc.h              the signal source's design, and why a self
                                  test was rejected for a feedback loop
    c/instr/pwmsrc.c              TIM1 channel 3 on PE13, the known frequency
                                  the counter needs in order to be shown to
                                  count at all
    c/instr/pwmmath.h             and its arithmetic, split out before the
                                  driver existed rather than after
    c/instr/pwmmath.c             the prescaler choice, the two off-by-ones and
                                  four refusals
    python/tools/gen_clock_vectors.py
                                  the oracle's double entry, run by hand

This project's own, in this directory:

    c/main.c                      first light: LED, banner, button
    c/pll280.c                    the raise to 280 MHz, and the measurement
    clock_vectors.json            the oracle: 20 decode rows and 8 bias rows,
                                  two of the 20 read off this board
    PROTOCOL.md                   the line protocol the C++ and Rust filters
                                  speak, so one parity test drives both
    cpp/clocktree.hpp             the decode in C++17, header only
    cpp/clocktree_filter.cpp      and the filter that drives it
    python/clocktree.py           the decode in Python, and the three places
                                  Python had to be made to behave
    rust/src/lib.rs               the decode as a no_std crate
    rust/src/main.rs              and its filter binary, p01-filter
    rust/Cargo.toml               the crate, a workspace member since
                                  Sunday 4 October 2026
    Use-CubeIDEToolchain.ps1      puts CubeIDE's bundled gcc, cmake and ninja on
                                  PATH for one session. Dot-source it; running it
                                  sets PATH in a child scope and looks like having
                                  done nothing
    Find-STM32Tooling.ps1         inventories every STM32, ST-LINK, Cube and
                                  SEGGER tool on a Windows machine, and the
                                  removable disk the probe presents, which is how
                                  a machine's ability to build and to flash are
                                  answered separately

The board support is under `c/board/` rather than in this project because every
project links it: changing it is a decision about all twenty at once. The decode
is under `c/clock/` rather than `c/board/` for a different reason, which
`clocktree.h` gives at length: it must stay compilable by a host compiler.

## Three decisions worth the space they take

**The vector table is C, not assembly.** The deliverable is a repository whose
every byte you can account for, and a table of function pointers is easier to
account for than a page of directives. Every handler is a weak alias for a
default that spins, so an unexpected interrupt lands somewhere a debugger can
inspect rather than running into whatever follows it in flash.

**The stack is in tightly coupled memory and transfer buffers are not.** DTCM is
zero wait state and the right home for the stack. It is also unreachable by the
main transfer engines, so the linker script provides a separate `.dma_buffers`
section in AXI SRAM, aligned to 32 bytes, which is one cache line on this core.
A buffer left in DTCM and handed to a transfer engine produces a transfer that
never completes rather than an error. That is P19's subject and the note is
written here so the defect never reaches P04.

**`-Og` and not `-O0` for debugging.** At `-O0` this core produces code so much
larger and slower that timing behaviour differs from anything shippable, and a
debug build whose timing is unrepresentative hides exactly the defects worth
finding.

## Not done

- No size figures and no map file reconciliation yet. The build writes a map file
  and `cmake/check_retarget.cmake` already reads it, so the reconciliation is a
  script that does not exist rather than a build that cannot produce the inputs.
- The interrupt vector positions below the sixteen architectural entries are
  this family's published ones and have not been read from RM0455. The table
  carries only what is certain and leaves the rest as the default handler, so an
  unconfirmed position cannot silently misroute.
- `python/tools/reconcile_size.py`, which the chapter asks for to sum the map file
  against the size output, waits for a build that produces either.
- Nothing touches option bytes, here or anywhere in this volume, until the
  question of whether a bad write can leave the board unrecoverable is settled.
