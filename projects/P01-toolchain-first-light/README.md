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
| The core clock, **measured seven times** | **279 672 822**, **279 435 368**, **279 714 764**, **279 634 208**, **279 624 968**, **279 541 148** and **279 692 180 Hz** against this board's 32.768 kHz crystal, 1168, 2017, 1019, 1306, 1339, 1639 and 1099 parts per million below the 280 MHz nominal. Always low, never the same, spanning 998 parts per million with no trend, so the sign reproduces and the digits do not, and four readings after the fifth have failed to widen that spread by one part per million. The cause is the debugger's clock output, which the seven runs put at 7 990 652, 7 983 868, 7 991 850, 7 989 549, 7 989 285, 7 986 890 and 7 991 205 Hz. Two further runs measured the reset clock only, because in each a second RESET press truncated the report before its core figure printed |
| The reset clock, **measured nine times** | **64 194 318**, **64 186 657**, **64 180 090**, **64 191 988**, **64 202 948**, **64 189 988**, **64 192 428**, **64 194 888** and **64 190 072 Hz** against the same crystal, spanning 357 parts per million where the PLL spans 998. Two explanations have been offered for that spread in this repository and the next reading refuted each one, drift by run 4 and temperature by run 5, so no third is offered. The spread was 357 parts per million on five readings, 357 on seven and 357 on nine, to the hertz each time, because every reading after the fifth has landed inside the span the first five set. What stands is a bound that four later observations failed to widen: this oscillator reproduces to 357 parts per million on this bench and no better |
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

## The core clock, measured nine times, and two explanations refuted

**This is the first frequency in this volume that is measured rather than
derived**, it does not agree with the nominal, and it does not agree with itself.
Nine runs on the same board, each gated by this board's own 32.768 kHz crystal
over one second. Runs 1 to 3 are Sunday 4 October 2026, minutes to hours apart.
Run 4 is Monday 5 October 2026 after about eight hours with the board away, so
cold. Runs 5 to 9 are Tuesday 6 October 2026: run 5 after about fifteen minutes
at 280 MHz, so warm; runs 6 and 7 after five minutes unpowered, so cold; and runs
8 and 9 with the thermal state not written down, which is recorded as a blank
rather than guessed at.

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

**And the spread has not moved through three rounds of this.** It was 357 parts
per million on five readings, 357 on seven, and 357 on nine, to the hertz each
time, because every reading after the fifth has landed inside the span the first
five set. The core's 998 parts per million has done the same across seven printed
core figures. A bound that four later observations failed to widen is a different
kind of statement from a bound measured once.

**So no third mechanism is offered.** Two were proposed from the pattern in hand
and the next observation removed each one. What survives is a bound and not a
cause: across nine readings this part's internal oscillator reproduces only to
**357 parts per million** on this bench, and nothing here may quote it, or any
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

**What it still does not show** is that the pin wiggles. Every readback confirms
the registers hold what was intended, including the `MOE` bit the output is gated
by, and none of that is the same as an edge arriving somewhere. Only the wire
settles that, and the counter is what will say so.

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

**The connector, which was the last thing blocking it, is settled as of Tuesday
6 October 2026.** Both pins reach the ST Zio header: PE13 is Arduino D3 and PD12
is Arduino D29. Neither source is ST, so neither was taken alone. Zephyr's board
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
