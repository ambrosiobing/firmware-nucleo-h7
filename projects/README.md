# The code for the twenty projects

The software for the twenty projects of this volume. The book and the code live
in one repository: `chapters/` and `figures/` are the written volume, and
`projects/` plus the shared `c/`, `python/firmkit/` and `python/tests/` are what
runs.

**Each project owns four languages.** Since Saturday 3 October 2026 every project
directory carries `c/`, `cpp/`, `python/` and `rust/`, each with a README saying
what that language carries for that project, what it is proven against, and what
it cannot do on this part. The four are meant to be equivalent and to be proven
equivalent against one oracle per project, which is the project's own: P09's six
hand-computed vectors, P05's published check value 0x29B1 together with its
corruption set, P08's eighteen transition rows. A language with nothing written
says so rather than holding an empty source file, because an empty file is a
claim.

**How equivalence is proven, and why it is one test rather than four.** Four
suites, one per language, would each pass while the four disagreed with one
another, because each would compare an implementation against its own idea of
the layout. `python/tests/test_parity.py` instead drives every available
implementation over one case list and compares them against the one that has run
on the board, which for P09 is the C. `assert_not_vacuous` then refuses a run in
which fewer than three of the four were actually compared, because a parity suite
that passes because nothing was built has proven nothing, and that is the one
failure such a suite cannot notice about itself. On win11 aquamarine that refusal
is a skip naming the WSL command, since two of the four reach the comparison
through a compiler that laptop does not run.

P09's table reads all four agreeing, first in WSL on bing@JPTOUPM678 on Saturday
3 October 2026 and then in the `rust` job of `code.yml`; on win11 aquamarine,
which has no cargo, it reads three and says so.

Two of the four are driven as filters, C++ and Rust, each project's pair
speaking one protocol: for P09, five integers per line in and the encoded bytes
plus the round trip out; for P05, the verbs `C`, `E` and `D` in and a checksum, a
frame or a verdict out; for P08, a sample and a list of event names in and the
final state, every row index taken, the counters and the stub's frame out; for
P02, one operation per line and the answer to that one operation, which is the
opposite shape to P08's and deliberately so, because P02's lists are long and a
long line is what broke P08's filter; and for P03, a step's five counters in and
the nine attributed fields out, with a second verb for the ramp's verdict; and
for P06, a capture's **path** in and the witness's twenty fields out, which is
the only filter here that takes a path, because a capture is thousands of
doubles and because a path is how the real witness receives one.

No filter parses its project's oracle, so that stays in the one file no generator
has ever touched, and `run_filter` in `conftest.py` drives all of them.

**Seven projects have a parity test so far**, P09, P05, P08, P02, P03, P06 and
P12, and all seven read four languages agreeing as of Sunday 4 October 2026, in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, with nothing skipped
and nothing failed. On win11 aquamarine the same suite reports 110 passed and 16
skipped, because compiling there is forbidden, and each skip names the command
and the laptop.

P06's is the only one compared **numerically** rather than exactly. Its witness is
floating point, and the contraction of a multiply and an add into one fused
instruction is not the same across four toolchains, so the doubles are compared
to a relative tolerance of 1e-12 while the verdicts and the counts are compared
exactly. The pass criteria are 0.1 percent, so that tolerance is nine orders of
magnitude tighter than anything the measurement claims. The capture that matters
is a clean wave at 1100 Hz: every implementation must fail it on the rate while
passing the jitter criterion, because a clean square wave at the wrong rate is
indistinguishable from a right one without an external witness, and that is the
project's whole subject.

P03's is the only one where the comparison is of the **analysis** rather than of
the thing analysed. Its receive path needs a peripheral and still refuses; its
attribution is arithmetic over counters a run recorded, and it is the half where
a wrong answer publishes a wrong conclusion rather than merely failing to work.
Every sent byte goes to exactly one of four places, and the one that decides
whether a run means anything is the subtraction: if bytes went missing before the
peripheral saw them, the run measured the bridge and the target's limit is still
unknown.

On win11 aquamarine, which runs no compiler, P09 reads three languages, P05, P08
and P02 read two, and P03 reads none at all, because its C reference is itself
new and that laptop cannot build it. Each says which, rather than passing
quietly, and the full comparison is what WSL and CI are for.

P02's is the one whose oracle is a trace rather than a value, and the one where a
language is honestly absent from half the subject. Its comparison is which bytes
were accepted, which refused and counted, the order they came back in, and the
two free-running counters, over 9620 operations in each of the four ordering
modes. What no host can compare is the cost of those modes, which is P02's actual
subject and a board figure.

P08's is the strict one, and the reason is that it has no external fact to appeal
to: P09 has hand-computed vectors and P05 a published check value, while P08's
table is itself the specification. So its test compares the **row index** each
implementation takes for every event. Two of its rows share a from-state and an
event and are told apart only by their guards, so an implementation that ordered
them the other way would give up after one transmit attempt instead of three, and
a comparison of outcomes alone might not notice.

**All five were proven able to fail by introducing defects on purpose**, which is
the only way to know a comparison compares anything:

| Project | The defects introduced, and what they turned red |
|---|---|
| P09 | one byte of a golden vector; one bit of the Python encoder, which named the case index and both encodings |
| P05 | the twin's polynomial, which turned both the check value and the frame comparison red; the twin's checksum test, which turned the corruption verdicts red with "Python says ok and C says checksum" |
| P08 | the two `TX_FAIL` rows swapped, which named both row lists; the retry limit changed to four; an undesigned exit added to the fault state |
| P02 | the capacity reduced by one, red at operation 256; the drop increment removed, red at 258; the 32-bit mask removed, which the trace lists do **not** catch and the wrap comparison caught at byte 3 |
| P03 | the verdict order reversed, which **was not caught**: no step lost bytes in both the bridge and the target at once, while three files called that order load bearing. Two steps were added and the order is now asserted by name |

The P03 row is the one worth reading twice. A mutation that went uncaught is a
finding about the test rather than about the code, and it is the second time in
one day that a hole turned up in a list of cases rather than in an
implementation.

Chapter NN of the book is the written design for project PNN. Start from
[the contents](../CONTENTS.md).

**Eight of the twenty have code.** 349 checks, of which 203 pass on a laptop with
no board, no probe and no Raspberry Pi; the other 146 need a compiler and skip
there, each naming the WSL command and the laptop. The remaining twelve projects
need hardware, and nothing in them is written yet rather than written and
untested.

**Four states, and the difference between them matters.** Until Friday 2 October
2026 this page had two, because nothing had ever been cross-compiled and the
distinction was not yet needed. A third was added that day and a fourth on
Saturday 3 October 2026, each the day something first occupied it:

| State | What it means |
| --- | --- |
| **runs on the board** | cross-compiled, flashed, and observed working on the hardware. P01, P02 and P09 |
| **links** | cross-compiles and links clean for the target, and has never been flashed, so nothing is known about its behaviour |
| **host only** | proven on a laptop against synthetic input, and never built for the target at all |
| **written** | the code exists and no compiler has seen it. Added Saturday 3 October 2026 for P09's Rust crate, which was written on the laptop that has no cargo and is not to get one. **No project is in this state as of Sunday 4 October 2026**, and the state is kept defined rather than removed, because every language of every project that has code passes through it for as long as it takes the next command to reach the laptop that compiles |

The middle state is the one worth being careful about. A target binary that links is
evidence about the build and about nothing else. P06's three back ends and P09's
codec both reached that state on Friday 2 October 2026. P09's codec has since run.
P06's timer back end was flashed on Wednesday 7 October 2026 and by that evening was
sampling at a nominal 1 kHz, which is what moved it out of this state; its other two back ends, systick and dma, have
still never been flashed and remain exactly what this row warns about.

**The state is written once and checked.** Each project README carries one line,
the same grammar in all twenty, with a state per language and `none` where nothing
is written:

    Status: c=board cpp=host python=host rust=none

`python python/tools/check_status.py` reads those twenty lines and refuses if a
language claims a state with no source file behind it, if a language says `none`
while its directory holds code, if a language directory has no README, if the
strongest claim disagrees with that project's row in the table below, or if any
Markdown here names a repository path that does not exist. That last check exists
because this page once said that nothing had run on hardware thirty lines below a
table of figures measured on hardware, and because three references to a C++
header named a path the file had never had.

| Project | What it is | State |
| --- | --- | --- |
| [P01](P01-toolchain-first-light/) | The toolchain, startup and linker script every other project needs | **runs on the board**, Friday 2 October 2026. LD1 green on PB0 blinks at 499.7 ms, `printf` reaches COM13 at 115200, the user button on PC13 reads, and the report carries the core, AHB and APB1 clocks decoded from RCC at startup. The delay loop measures itself against `DWT_CYCCNT` every boot, 9 cycles per iteration, and a 100 ms request lands within 20 parts per million. The oscillator measured 64.17 to 64.18 MHz across six reductions. The 280 MHz tree was **reached on Sunday 4 October 2026** by a second image, `p01-pll280`: core 280 MHz, AHB and APB1 140 MHz, eight gated steps each reading back what it wrote. The core clock is now **MEASURED**, sixteen times, against this board's own 32.768 kHz crystal: 279 672 822, 279 435 368, 279 714 764, 279 634 208, 279 624 968, 279 541 148, 279 692 180, 279 424 340, 279 522 728, 279 348 956, 279 486 828, 279 411 320, 279 551 130, 279 352 464, 279 359 440 and 279 247 016 Hz, which is 1168, 2017, 1019, 1306, 1339, 1639, 1099, 2056, 1705, 2325, 1833, 2102, 1603, 2313, 2288 and 2689 parts per million below the 280 MHz nominal. Always low, never the same, spanning 1037 parts per million with no trend, so the sign reproduces and the digits do not. That spread held at 998 through seven readings and the eighth widened it, which withdrew a claim made here an hour earlier that it had stopped moving; the count also read seven against six listed figures for that hour, because the number was changed and the figure was not. The cause is the debugger's clock output rather than this part, and the relative movement is what shows it: both clocks in a run share one crystal gate, and the external one moved against the internal one by -730, then +1102, then -473, then -204 parts per million, a figure that changes sign twice and therefore cannot be the shared reference drifting. The reset clock was measured twenty-three times, seven more than the core, because a second RESET press truncated one report before its core figure printed. That oscillator has refuted two explanations in a row, each written here from the data of the moment: drift, removed by run 4 coming in higher than run 3, and temperature, removed by run 5, which was taken warm on purpose to test the prediction that a warm reading would fall toward 64 180 000 and came back at 64 202 948 Hz, the highest of all seven. Runs 6 and 7 were then taken cold to ask whether cold and warm differ at all, and both landed inside the existing span. So no third explanation is offered and what stands is a bound: that oscillator reproduces to 357 parts per million on this bench and no better. A third pattern is visible, the cold readings spanning 68 parts per million against the warm ones' 357, and P01's README names it, declines to claim it on three stated grounds, and writes down the experiment that would test it. The crystal was itself cross-checked by measuring the reset clock, where it agrees to within 379 parts per million with the 64.17 to 64.18 MHz two other instruments found, which excludes the alternative explanation by a factor of three. This is the first frequency in the volume that is measured rather than derived. The decode that produced the figure it was checked against moved to c/clock/clocktree.c on Sunday 4 October 2026 so that a host can drive all eight of its refusals, against 28 hand written rows in projects/P01-toolchain-first-light/clock_vectors.json, two of which are this board's own register words, through the same object file the board links. That oracle is also what finally gave P01 a host half, so the project now carries all four languages: the decode in C, C++, Python and Rust, compared answer for answer including the refusal tokens, with the board half of the other three still to come. The two clocks are wrong in OPPOSITE directions, 3027 ppm low at the reset clock and 1170 ppm high at the 280 MHz setting, so a figure taken at one and the same figure taken at the other carry errors of opposite sign; P01's README has the table. Nothing published is wrong, because nothing quotes a time to better than a part in a thousand, and the table is for the next figure that wants to. Five runs and four causes: three register facts read out of ST's headers before writing, including a voltage scale encoding that is reversed between this part and the H743, and two found by the board, that voltage scale 0 is reachable only from scale 1 and that no voltage scaling works until a supply is selected. A sixth cause was mine: selecting the LDO stopped the part, because this board is supplied through the SMPS, and a power cycle recovered it. THE FREQUENCY COUNTER is in this image too, LPTIM1 counting edges on PD12 against the same crystal gate, and through seven runs it reported 0 millihertz because nothing was ever connected to that pin, which is both the correct reading for an idle line and what a refusal returns. Since Tuesday 6 October 2026 the image also carries the signal source, TIM1 channel 3 driving PE13 at one megahertz, started before each counter reading and re-planned at each clock so the same output comes from different prescaler fields. It was flashed on Tuesday 6 October 2026 and the source came up at both clocks, which settled three things no host could: that RCC_APB2ENR is at 0x150 and not the 0xF0 in ST's own comment, because a wrong address would have left TIM1 unclocked and its registers would not have held the writes that were read back; that the APB2 decode is right, the source reporting its timer clock as 64 MHz then 140 MHz with CDPPRE2 at divide by one; and that pwmmath's fields are right against hardware rather than only against a table, PSC 0 with ARR 63 at 64 MHz and PSC 0 with ARR 139 at 140 MHz, both producing exactly one megahertz. ON TUESDAY 6 OCTOBER 2026 THE WIRE WENT IN AND THE COUNTER COUNTED, which is the claim this project could not make from Monday 5 October 2026 until that moment. One jumper, CN10 pin 10 to CN10 pin 21, both positions sourced from ST's own readme files under Projects/NUCLEO-H7A3ZI-Q in STM32Cube_FW_H7_V1.13.0. Edges arrive on PD12 and LPTIM1 counts them. The number was also wrong, low by a factor of eleven to twelve, and the size named its own cause: freqcount_measure_mhz samples the counter twice and ARRM is a flag meaning one or more matches rather than a count, so a gate spanning fifteen contributes one. A falsifier was written before any fix, a gate of twelve ticks too short to wrap twice, and it confirmed the model: the short gate reads about 1.03 MHz where the refuting band was 65 to 131 kHz. The same capture then exposed a SECOND defect, which is the better find. The short gate should have agreed with the clock offset in parts per million, since the source follows the clock and the gate is the crystal; it came back 32768 to 41877 ppm out and on the wrong side of nominal at 280 MHz. The implied gate is 12.5 ticks rather than 12, because freqcount brackets lseref_measure_core_hz rather than owning the gate, so the edge window includes a boundary wait that the divisor does not. The counter's window is wider than the gate it is divided by. Both had one cause and BOTH ARE NOW FIXED AND CONFIRMED on the board, runs 15 and 16 of Tuesday 6 October 2026: freqcount owns the gate loop, opening the tick source itself, waiting a tick so the first sample sits on a boundary, polling and clearing ARRM thousands of times per tick instead of twice per gate, and sampling at the two boundaries it controls. The confirmation is not a frequency but the IMPLIED GATE LENGTH, which went from 12.39, 12.46 and 12.50 ticks where twelve were asked for, always above, to 12.002, 11.999 and 12.002, straddling. Always above is a bias and straddling is noise. The one second reading went from 87 938, 88 101 and 82 550 Hz against a megahertz source to 1 002 684, 1 002 805 and 997 897. An interrupt was rejected on the facts: ARRM is a flag and not a counter, so a handler late by one autoreload loses a match exactly as two samples did. THE BEST READING IN THIS VOLUME came out of it, run 16 at 280 MHz agreeing with the crystal-derived figure to 0.6 parts per million, inside that gate's own resolution. What was left was a short-gate scatter of about 140 ppm which CHANGES SIGN, so noise and not bias, and the only unmeasured term in it was the time one pass of the gate's waiting loop takes. THAT IS NOW MEASURED, with the threshold published before the board ran it: near 1200 passes per tick and the loop's granularity accounts for the scatter, near 20000 and it does not. Runs 18 and 19 gave 2298 passes per tick at the reset clock and 3925 at 280 MHz, so the granularity is the DOMINANT term and together with the edge quantisation reaches 115 ppm of about 138 observed, which is most of it and not all. The same two numbers settled a second question for nothing: the pass time fell by 1.71 when the core rose by 4.375, so THE LOOP IS NOT CORE BOUND, and the fifty core cycles this repository's own comment had estimated turns out right about the core work and silent about the larger fixed latency, which is not half right but confidently wrong about the total. AND THE OPEN QUESTION HAS MOVED, which is the result that matters most. Run 19 at 280 MHz returned a long-gate residual of +407 ppm against an instrument budget of 5.4 ppm over that gate, so it CANNOT be the counter, by arithmetic rather than judgement. The two instruments measure one quantity seconds apart and the core readings at 280 MHz span 710 ppm across three runs, so the next measurement is of the clock and not of the counter. THAT IS NOW WRITTEN with its threshold published first: lseref_measure_core_hz run more than once inside one boot, at two separations, about one second and three to four. Near 400 ppm and the counter's residual is the clock moving; near 1 ppm and that residual is something else and a NEW problem; between the two and it is part of the story only. The middle outcome is listed rather than left out, because an awkward result with no entry written beforehand gets read charitably afterwards. It is also a ratio and not the core alone, since both readings are core cycles over a crystal gate. AND THE BOARD ANSWERED: runs 20 and 21 give 18, minus 125 and 14 ppm over one second and minus 164 and minus 383 ppm over three to four, so the clock holds over one gate and MOVES over four seconds, in either direction, which is the first of the three outcomes. The counter is therefore exonerated by measurement and not only by budget arithmetic: where both were measured in one run the drift and the counter's residual agreed in sign three times out of three, at +18 against +37, minus 164 against minus 157, and minus 383 against minus 152. A CONSEQUENCE NOT ANTICIPATED: every spread figure on this page is built from one reading per run and no two were taken at the same point in their runs, so the 579 ppm and the 1306 ppm were never bounds on run-to-run reproducibility alone and contain within-run drift as well. Both remain correct as bounds on what this bench reproduces; neither is a figure for an oscillator's stability, and the pages had been calling them that. One surprise is named and not explained: at 280 MHz the chain runs from the debugger's quartz-derived 8 MHz and the gate is a 32.768 kHz quartz, so 383 ppm in four seconds is large for anything with quartz on both sides. AND THE SHAPE OF THAT MOVEMENT IS THE NEXT QUESTION, because three readings cannot tell a ramp from a step from a staircase from wander and the four mean different things: 48 gates of 128 ticks run back to back, 24 seconds at the 280 MHz clock, same instrument again. The statistics were chosen before the data and the first choice FAILED its own test before any flash: counting reversals of direction scored a noisy step at 34 of 47 and read it as wander, because a step is flat on both sides and a few ppm of noise on a flat region reverses constantly. The biggest single step plus how many steps exceed a quarter of the span replaced it, with how many of those go up added after a staircase read as wander too; fourteen synthetic series across four shapes, clean and with 3 and 30 ppm of noise, all read correctly. AND RUN 23 ANSWERED IT: WANDER. Forty-eight readings span 990 parts per million inside one 24 second window, sixteen steps larger than a quarter of the span, six up and ten down, so not a ramp and not a step. THAT ONE FIGURE EXPLAINS THE WHOLE TWO DAYS: 990 ppm in one window against 1671 across sixteen separate runs are the same order, so none of the spread figures this volume quoted was a convergent estimate; each was one sample of a wandering quantity, and a spread across N samples of such a quantity grows with N. Five widenings, six holds and a seventh widening on runs 22 and 23 is what that looks like, and it is why drift, temperature and two claims of settlement were each removed by the next observation: there was no pattern to read. The figures stay usable and the word changes, from reproducibility to what has been SEEN to span. It also withdraws two claims made here two commits earlier, both from three samples: the drift agreeing in sign with the counter's residual is now 2 of 4 on the consistent comparison, and the clock holding over one second while moving over four is far weaker than it looked, the one second figures having reached 230 ppm against the four second 383. What survives of that commit is its arithmetic rather than its patterns: the counter's budget over a one second gate is 5.4 ppm, so residuals of hundreds were never the counter, and that elimination still holds. The page also corrects its own earlier description of the short gate: it is the better gate-width detector and the worse frequency meter, and only the flattering half had been written |
| [P02](P02-ring-buffer/) | A single producer, single consumer ring buffer | **runs on the board**, four targets one per ordering mode, producer in thread mode and consumer in SysTick. Re-measured Saturday 3 October 2026 with the instruction cache on, which overturned the first table: cold, the DMB build measured 2 cycles FASTER than no barrier at all, so that comparison had the sign wrong, not merely the magnitude. On the cached figures the compiler fence costs 0.00 cycles and 0 bytes while still emitting different instructions (same length, different SHA-256), one DMB in put and get costs 22.00 cycles and 16 bytes, and acquire plus release costs 29.00 and 24. The earlier claims of 7 and 17 cycles are withdrawn. Zero mismatches in 21.4 million bytes across the four. Also host tested in all four modes, 21.6 million bytes per run. Four implementations, all four proven on the host Saturday 3 October 2026 and agreeing over 9620 operations in each of the four modes. Python is deliberately absent from half the subject, because the interpreter has no release store, no acquire load and no barrier, so it joins the trace comparison and not the ordering one. The Rust crate names a second absence: the C's one-struct-two-contexts shape is not expressible in safe Rust at all |
| [P03](P03-interrupt-receive/) | Receiving on interrupt without losing bytes | host only. The measurement is proven, attributing every lost byte to the target or the bridge. `rx_ring.c` and both documented failure modes are written and have never been built for the target. Four implementations of the attribution since Saturday 3 October 2026, which is the half that can be proven, all four agreeing over 19 synthetic steps and four ramps; the Python is the one the other three were written to be compared against |
| [P04](P04-dma-idle-line/) | Circular DMA and the idle line | not started. Needs a transfer engine |
| [P05](P05-framing-crc/) | Framing and the hardware CRC unit | host only, and proven there in all four languages, Saturday 3 October 2026: every one reaches the published check value 0x29B1, produces byte-identical frames over 3008 payloads, and gives the same verdict on 3005 frames corrupted three ways. The C++ and the Rust assert 0x29B1 at compile time, which the C can only assert in a test. Writing them found one divergence worth keeping: the C names the single byte 0x01 a stuffing error where the other three name it too short, because the C's cobs_decode cannot tell an empty frame from a failed one. The peripheral half waits on RM0455 |
| [P06](P06-timer-sampling/) | Sampling on a timer at exactly 1 kHz | **runs on the board** since Wednesday 7 October 2026, and the state is claimed narrowly: the TIMER back end was flashed and reported, and the other two, systick and dma, still link and have never been flashed. THE ROW BELOW IS A SEQUENCE AND NOT A STATE, so read the capitalised markers in order: the timer image went from three of the converter's eight steps to sampling in five board sessions on one day. AT THE FIRST OF THEM it brought the converter up through three steps and refused the fourth by name, so nothing had sampled and the project's claim of 1 kHz was unmade. It is STILL unmade at the end of the row, for a different and better reason. FLASH went 9476 to 11468 bytes. Five captures, one from a single clean RESET press, all byte identical, and every line predicted before the flash. It settled four things no host could. The timer clock is 64 000 000 Hz on the reset clock with CDPPRE1 dividing by one and TIMPRE clear, which is `pwmmath_timer_hz` answering a question this file returned -2 for until that morning. ADVREGEN is bit 28, written and read back as 0x10000000. RCC_AHB1ENR_ADC12EN is the correct bus clock enable, by the same argument that confirmed RCC_APB2ENR at 0x150: a wrong enable means no ADC write sticks, and two of them stuck. And the ten microsecond regulator start-up is measurable on the cycle counter at 64 MHz, 640 ticks, rather than counted in loop iterations. THE NOTABLE NON-EVENT is that the ADC regulator accepted a direct write from reset, which was worth watching for because the voltage scaling on Sunday 4 October 2026 did the opposite: scale 0 is reachable only from scale 1 and the regulator declined a direct write in silence. The analogous trap was looked for here and did not occur, and an absence that was specifically tested for is worth recording. ONE DEFECT IN THE REPORT ITSELF was exposed by the capture: the deep power-down step prints `wrote 00000000 read back 00000000`, which is equally consistent with DEEPPWD having been set at reset and cleared and with it having been clear already, because the step does not print the as-found value first. So this run cannot claim deep power-down was exited, only that the bit reads clear afterwards, and the fix is the one c/board/clock280.c already made when its dump was relabelled "as found". LATER THE SAME DAY IT REACHED FIVE OF THE EIGHT STEPS, written, built and run, so the three step capture above is superseded. FLASH 11468 to 12740 bytes, three captures, one from a single clean RESET press, all identical. Step 4 had been blocked not by an unread register but by a choice nobody had made, which ST also leaves to the caller by branching on it in ADC_ConfigureBoostMode: the choice is now synchronous with CKMODE dividing by four, the slowest ratio that field can express and therefore the most conservative available while this part's maximum ADC kernel clock is still a datasheet question, at the cost of a 16 MHz kernel clock where divide by one would give 64. Five more register facts came back confirmed by read-back: DEEPPWD is bit 29 and SET at reset, which is what finally lets the page say deep power-down was EXITED rather than merely clear afterwards; ADVREGEN is bit 28; ADCAL is bit 31 and self clearing, taking 180 polls; BOOST is two bits at bit 8; and CKMODE field value 3 is divide by four at bit 16 of the COMMON block. The kernel clock arithmetic came out exactly as adcmath computed it on a laptop that compiles nothing for this part, 16 MHz from HCLK 64 MHz over 4 with boost 1 from the halved 8 MHz. THE ENABLE LOOP TOOK FOUR PASSES, WHICH WAS READ AS THE ERRATUM AND SETTLED LATER THE SAME DAY AS NOT BEING IT. Four passes of a loop that both writes ADEN and polls ADRDY fits two stories equally, so the mechanism was declined and an experiment written: try one write first and fall back to the loop only if it fails, which answers the question in one flash and leaves the converter enabled either way. The prediction was written before the flash, from arithmetic rather than hope: ST s condition is ADEN within four ADC clock cycles of ADCAL, and two printf calls sit between calibration and enable, about four milliseconds at 115200 baud or some sixty four thousand cycles of the 16 MHz ADC clock, so the window cannot be met by this code. THE BOARD AGREED AND THE NUMBER IS WHAT MAKES IT CONCLUSIVE: ready after 4 polls with NO re-assertion, the same count the loop had reported, so had the re-assertions mattered the single write would have taken longer or refused. The claim is scoped narrowly and is not that ST is wrong: this image does not meet the condition, so the workaround is unnecessary here and the rule stands for any sequence that does meet it. The loop is kept for exactly that reason, running only when the single write is not enough, and acq_timer.c says so where it sits so it is not deleted as dead code by somebody measuring only this build. One bit is named rather than guessed: the status register read 00001001, so besides ADRDY at bit 0 something at bit 12 is set and this repository has not read what it is. THE BRING-UP COMPLETED THAT EVENING AND THE CONVERTER NOW SAMPLES. Steps 6, 7 and 8 were written and acq_start returned 0 for the first time in this project's history, and the last open item, the channel, came off the list without the datasheet: ADC channel 13 on PC3 at CN9 pin 5 is stated twice in ST's own NUCLEO-H7A3ZI-Q example ADC_DualModeInterleaved, in its readme and in its main.h, so stm32h7a3zi.pdf remains unread. EXTEN 1 for the rising edge, RES 0 for sixteen bits and SMP 7 for 810.5 cycles were read from the LL header rather than inferred from bit counts, and the sampling time is recorded as a choice with its arithmetic because ST's five examples for this board disagree about it. AND THE EIGHT STEPS ALL REPORTED OK AND NOT ONE SAMPLE ARRIVED, which is the most useful thing that happened to this project. The console stopped mid-word at the instant TIM6 was enabled. Two writes had been missing since the file was written, EOCIE in the converter's own IER and ADSTART, both found by reading the file once it stopped refusing, and adding them was necessary and was not sufficient. The third cause was that the vector table in c/board/startup.c had NO DEVICE INTERRUPT ENTRIES AT ALL: ADC_IRQHandler existed, was weak, compiled and linked, and was simply not in the table, so the first end of conversion fetched a vector from past the end of the array and branched into .text. THREE THINGS MUST BE TRUE FOR A HANDLER TO RUN AND ONLY TWO OF THEM READ BACK: the peripheral's own enable, the controller's enable, and a vector. The third has nothing to read back from, which is why it was the one that went missing while every step report said ok, and the comment already sitting in that table had predicted exactly this outcome and was never cashed in. WITH THE VECTOR INSTALLED IT SAMPLES: FLASH 14192 bytes, .isr_vector 64 to 224, and twenty six report lines over roughly twenty five seconds, 390 blocks of 64 conversions, 24 960 conversions, overruns 0 throughout. The whole chain runs, TIM6 update to TRGO to a hardware-triggered conversion with the processor out of the path, to the controller, the vector, the handler, the block and main. Three counters that do not read each other agree: seq from the handler equals blocks from main on all twenty six lines and overruns stays zero, and a single lost block would move all three. AND THE RATE IS STILL NOT MEASURED, which is the point this project refuses to let slide. Half the prediction published before the flash could not have failed: blocks rising by 15 per line is ACQ_RATE_HZ over the block size, 1000 over 64, arithmetic in main.c rather than anything the board decided, and the comment above that line claimed the cadence did not depend on the thing being measured, which is false and is corrected in the same commit. The only rate observation is the wall-clock cadence of the lines read against a PowerShell sleep loop: 330 blocks between two markers twenty nominal seconds apart is 1056 Hz, an upper bound rather than an estimate because the loop's own overhead makes the elapsed time longer, so it is consistent with 1000 Hz and is not evidence for it. None of MEASUREMENT.md's four criteria is touched, because all four are about the instants at which conversions happened and nothing in the capture observed an instant. The board cannot settle this in principle: any timebase on this die that could count the interval is derived from the same oscillator that defines it, so firmware timing itself would confirm a wrong rate as readily as a right one. The MCC 118 on the Raspberry Pi watching the marker on PB4 at CN7 pin 19 is the measurement, the wire is not connected, and until it is the 1 kHz in this project's title is a claim. The value is not the result either: mean reads 3 to 5 of 65 535 on a floating PC3, said in advance to be meaningless, and the only thing in it worth a sentence is that it varies between reports, which is weak evidence that the conversions are real rather than a stuck register. ONE GAP WAS NAMED IN THAT CAPTURE AND CLOSED THE NEXT DAY. OVRIE was not enabled and the converter's own OVR was not read, so overruns 0 across 390 blocks could not have been anything else: nothing watched the counter the application's own cannot cover. On Thursday 8 October 2026 the timer back end got both halves in one commit, the interrupt enable and an OVR that the handler counts and CLEARS, because enabling a line whose flag nobody clears would hold the part in the handler and that is worse than the hole being closed. Two numbers are now reported side by side and they answer different questions: overruns is main being too slow to collect a finished block, convovr is the converter unable to deliver a conversion because the previous result was still in DR, which is the interrupt being too slow or not arriving. Either can be zero while the other is not, and a rising convovr with overruns at zero would mean the handler is losing samples the application never hears about. OVRMOD is printed AS FOUND and not set, following the as-found discipline the deep power-down step earned, because the bit decides what an overrun does to the data and choosing it would be a configuration decision presented as a fact. acq_conv_overruns returns -1 rather than 0 from the other two back ends, so 0 means the flag was watched and never set while -1 means nobody looked, and main says which before the first report line. The dma build is where -1 is least comfortable, since a transfer engine reading DR is exactly where OVR is the primary failure mode rather than a remote one: it most needs the flag and is least entitled to report it. ON THE BOARD THAT EVENING OVRIE READ BACK AS BIT 4, IER wrote 00000010 and read back 00000014 which is OVRIE beside the EOCIE already set at bit 2, and OVRMOD READS 0 AS FOUND, so an overrun preserves the data register and discards the new conversion. convovr was 0 on every line of both runs in one capture across a reset, 3135 blocks and 200 640 conversions with overruns also 0, which is eight times the Wednesday 7 October 2026 soak and is the first pair of zeros here that could have come out otherwise. AND THE CAPTURE CORRECTED THIS ROW'S OWN RATE CLAIM. The 1056 Hz figure recorded above was called an upper bound on the reasoning that loop overhead lengthens the elapsed time; the same image in one capture then gave 1056.0 Hz before the reset and 960.0 Hz after it. A true 1000.0 Hz emits one report line every 0.9600 s, so 10.4167 lines per ten second window, so every such window must show 10 or 11 lines, which is 960.0 or 1056.0 Hz and nothing between. The error is two-directional quantisation rather than one-directional overhead, so upper bound was the wrong shape of claim, and what the two readings establish is weaker and cleaner: they bracket 1000 Hz at about ten percent resolution. THE CORRECTION OPENS A ROUTE THAT HAD BEEN WRITTEN OFF. The board still cannot time itself, but the HOST CLOCK IS AN INDEPENDENT TIMEBASE, so the console is a coarse external witness rather than none, and stamping each line's arrival removes the quantisation instead of shrinking it. MEASUREMENT.md gains a section, appended and not edited, fixing the method and the threshold BEFORE the run: a 600 second stamped capture, 999.0 to 1001.0 Hz passes, and two consecutive runs disagreeing by more than 0.05 percent withdraws the number rather than averaging it. It can settle criterion 1 and NOTHING ELSE, because the console sees block completions 64 samples apart and cannot observe an interval, so criteria 2, 3 and 4 still need the MCC 118 on PB4 and those three are the chapter's actual subject. ONE OBSERVATION IS OPEN AND IS NOT THE CLAIM: mean read 3 to 5 on Wednesday 7 October 2026 and 3037 to 3397 on Thursday 8 October 2026 with nothing deliberately connected to PC3 either night, 0.2 mV against about 162 mV at a 3.3 V reference. A frozen data register would have explained the first reading and is ELIMINATED by data already in hand, since blocks incremented at the nominal rate all night and that requires EOC once per conversion and therefore DR written each time. So both were live readings of a floating pin and the converter is not implicated; what changed is the pin's environment and this repository does not know what. The control is one jumper wire from PC3 at CN9 pin 5 to a GND pin, with its threshold published first: mean within a few counts of 0 and steady, and a reading near 3200 with a wire to ground would mean the floating input is not the cause. The witness is proven on synthetic input on the host. Each back end still refuses at run time rather than guessing a converter, timer or transfer engine setting RM0455 governs. The cycle counter it needs now exists and works, which was the first of its dependencies to be settled. Four implementations of the witness, **proven equivalent on Sunday 4 October 2026** over nine synthetic captures, all four agreeing on all nineteen fields of each. This is the sixth project with four languages and the first compared numerically rather than exactly, to a relative tolerance of 1e-12, which is nine orders of magnitude tighter than the 0.1 percent the measurement claims. Three language findings came out of it. The Rust crate is the only one in the workspace that is not no_std, because f64::sqrt lives in std and not in core, while rate.c compiles for the target unchanged, so the same program is portable to this part in C and is not in Rust without a crate. Clippy refused the Rust alone of the six crates, on a comparison against a budget constant set to the minimum of its type, and its suggested replacement would have inverted the criterion, so the lint is silenced by name with the argument beside it. And the C alone drew four -Wdouble-promotion warnings, because C defines NAN as a float, where the C++ had already written std::nan("") |
| [P07](P07-stop-mode/) | Stop mode, RTC wake, and a battery number | not started. Needs the PPK2 and a running clock tree |
| [P08](P08-node-state-machine/) | The node's state machine, transmit as a stub | written and proven: all 18 transition rows reachable, and the stub payload matches P09's encoder byte for byte. Four implementations, all four proven on the host Saturday 3 October 2026 and agreeing on **every row index taken** over twenty sequences and 2674 events, which pins the tables to one order rather than only to one behaviour. The Rust crate is the workspace's first inter-crate dependency, on P09's, so the shared criterion holds by construction |
| [P09](P09-payload-codec/) | The payload codec and its Python twin | **runs on the board**, Saturday 3 October 2026. The negative golden vector, feature -1, encodes to 2405FFFFE8 on the Cortex-M7, byte for byte what the host produces, and decodes back to the value it started from, so sign extension, bit order and field packing agree between the two compilers. It also carries the volume's measurement of placement: four images displaced by 0, 16, 32 and 48 bytes and otherwise identical, where the cold cost tracks the offset within a 32 byte granule (3220.99 cycles at offset 0, 3163.98 at offset 16, the two images at each offset agreeing to the hundredth) and the cached cost is 2378.99 in all four. Host half: all four implementations, C, C++, Python and Rust, agree over the six vectors and 100000 recorded-seed cases, proven Saturday 3 October 2026 in WSL on bing@JPTOUPM678 with rustc 1.99.0, and the Rust library compiles for the board's target, which is the proof it is genuinely no_std |
| [P10](P10-energy-phases/) | Where the energy goes, by phase | not started. Needs the PPK2 and marker pins |
| [P11](P11-at-engine/) | An AT engine that never blocks | not started. Needs the SIM7020E |
| [P12](P12-energy-regression/) | Energy as a regression test, and the rig | both gates written and proven: a five percent charge regression turns the build red with nobody at the bench. The hardware job is not built. Four implementations of both gates, **proven equivalent on Sunday 4 October 2026** over twenty eight cases, eleven for the size gate and seventeen for the charge gate, reaching every refusal and every failure the protocol defines. This is the seventh project with four languages and the only one where the **Python** is the reference rather than the C, because the gate that actually runs is the Python one. Two findings came out of it. The Rust crate is `no_std` only because its one piece of floating-point maths can be spelled without `f64::abs`, which lives in std for the same reason `f64::sqrt` does, so P06 and P12 are now two data points on the same question rather than one anecdote. And the C is the only one of the four that could compile for the target, which costs it about sixty lines of fixed buffers, bounds checks and an insertion sort that the other three do not have |
| [P13](P13-iks4a1-fifo/) | The IKS4A1: FIFO, watermark, interrupt | not started. Needs the shield |
| [P14](P14-iks5a1-dual-range/) | The IKS5A1: low and high g at once | not started. Needs the shield |
| [P15](P15-time-of-flight/) | Eight by eight time of flight on the MCU | not started. Needs the shield |
| [P16](P16-classifier-placement/) | Where the classifier runs: sensor, MCU or host | not started. Needs the shield and a Raspberry Pi |
| [P17](P17-hal-against-registers/) | The same driver twice | not started. Needs a running target to measure |
| [P18](P18-transforms-filters/) | Transforms with a numerical acceptance test | not started. The host reference could be written now; the board half cannot |
| [P19](P19-caches-mpu-dma/) | Caches, the MPU, and stale DMA reads | half done and measured, Saturday 3 October 2026, out of sequence because it blocked every cycle figure in the volume. The instruction cache enable is architectural, read from ARM's cachel1_armv7.h rather than from RM0455, and is in `c/board/icache.c`. Measured on P09: the cache is worth 1.33 to 1.35 times, and more importantly it removes the placement dependence entirely, so a cycle figure becomes a property of the code rather than of the build. Pure placement is worth 57 cycles, 1.8 per cent, which is at most a quarter of the 7.8 per cent once attributed to it. Still missing: the data cache half, which needs a transfer engine to make a stale read possible, and that needs RM0455 |
| [P20](P20-rtos-task-set/) | Three jobs at once, as an RTOS application | not started. Rebuilds P08 to P12, so it waits on them |

**What "proven" means here, and what it does not.** Every project marked proven has
tests that fail when the thing they check is broken, and several were themselves
tested by introducing defects on purpose. Proven on the host and measured on the
board are different claims, and the table above is arranged to keep them apart.
**Every figure here is one of three things, and says which: a measurement taken on
the board with its instrument and its date named, arithmetic, or a count of
synthetic cases.** Where a host object size appears it says so, because it is
explicitly not the flash cost on the board.

## Why there are shared directories

Most of these projects are the same shape underneath. P03, P04, P05 and P11 all
move bytes through one ring buffer. P08 builds the frame P09 encodes, P12 tests
it on a runner with no board, and P11 sends it over a modem. P02, P06, P17, P18
and P20 all want the cycle counter. Those parts are not properties of whichever
project first needed them, so they live together.

So the rule is: a project's own code lives in that project, and the top level
holds only what more than one project uses. Until Saturday 3 October 2026 it was
the other way round, with `c/P01` through `c/P09`, `cpp/P09` and five `python/PNN`
directories split by language at the top, and a reader who wanted one project had
to visit four trees to find it.

| Path | What it is |
| --- | --- |
| `c/ring/ring.{c,h}`, `c/ring/barrier.h` | P02's structure and its four ordering modes, used by P03, P04, P05, P11 |
| `c/payload/payload.{c,h}`, `c/payload/payload_fields.h` | P09's codec, used by P08 and P12. The header is generated |
| `c/instr/` | the cycle counter and the gated frequency counter, the instruments the board provides about itself |
| `python/firmkit/payload.py` | the Python twin: test oracle, edge-host decoder, and the same file again on the board under MicroPython |
| `python/firmkit/rate.py` | P06's edge and rate analysis, two independent routes that must agree |
| `python/firmkit/cbor.py` | the serialisation-size arithmetic, so a format decision carries a number |
| `projects/PNN-name/` | each project: its specification, design notes, fixtures, README, and its own `c/`, `cpp/`, `python/` and `rust/` |
| `python/tests/` | every suite, central, none of which touch a device |
| `python/tools/` | the generator, the host build, and the checks that prove the checks work |

## Three rules this repository obeys

**Nothing here needs hardware.** No check in this suite talks to a board, a probe
or a Raspberry Pi, so the CI runner is a fair test of it rather than a reduced
one. If something needs a device it sits behind a command the suite does not
call, and its project README says so.

That is narrower than what this paragraph claimed until Wednesday 7 October
2026, which was that the whole suite runs on a laptop with no board. **It does
not, and it never did.** 146 of the 349 checks need a compiler, and the authoring
laptop is forbidden to run one, so they skip there and run in WSL and in CI.
Needing no hardware and running everywhere are two different claims, and only
the first one is true.

**A refusal beats a guess.** Where a value is not confirmed against RM0455, the
code returns an error rather than running with a plausible setting. P06's three
acquisition back ends all do this today. A board that runs and lies is worse than
one that refuses, and a clean square wave at the wrong rate is indistinguishable
from a right one without an external witness.

**Every measurement names its instrument, or says it was not taken.** A budget row
reading "not measured" is correct and finished. A plausible number is neither. The
figures that exist were taken with the board's own cycle counter and its gated
frequency counter, named in the row that carries them. Every other row still reads
"not measured", and that is the honest state rather than a gap to be filled with an
estimate.

## The one thing that makes most published material wrong for this part

**This is not the STM32H7 the internet is about.** The STM32H7A3 is documented by
**RM0455**. The popular member of the family, the STM32H743, is documented by
**RM0433**. The clock tree, the power and regulator configuration and the memory
map all differ. Code copied from material that says "STM32H7" does not run
slowly; it produces a board that does not boot.

So no linker script, clock configuration, memory map or register sequence is
inherited from a sibling part without being checked against RM0455.

## Running it

Every repository here carries its own `.venv` in its root, so nothing is
installed into the system interpreter and two volumes cannot disagree about a
package version. Create it once:

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python -m venv .venv
```

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; .\.venv\Scripts\activate
```

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python -m pip install -r requirements.txt
```

Then, with the environment active:

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python python/tools/build_host.py
```

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python -m pytest python/tests -q
```

Eighty-seven checks, about ten seconds, on a machine with no board attached and no
compiler invoked.
Each project's own README gives its flow.

`.venv` is ignored by git, so it is built from `requirements.txt` rather than
committed.

> **If the build fails with the library open.** Windows will not let a loaded
> library be overwritten, so `python/tools/build_host.py` refuses and says which file,
> rather than letting the linker produce an error that does not mention locking.
> It is nearly always a `pytest` run in another window or a Python session that
> imported the library through `ctypes`. Close it and run the build again.
> Nothing is wrong with the source.

## Which laptop does what, and why it matters to a reader

Three machines, and no step is interchangeable between them. A command here always
names the one it is for.

| Laptop | What it does | What it does not |
| --- | --- | --- |
| **win11 aquamarine**, the authoring laptop | the volume, the host suite, the checkers. `python -m pytest python/tests -q` and every `python/tools/check_*.py` that needs no compiler, which is all of them except `check_c_syntax.py` and `check_ring_assert.py`, both of which refuse here by design. The count is deliberately not written: this sentence said three on Tuesday 6 October 2026 when there were five such scripts, and a count in prose is the thing that drifts | compiles nothing, cross or host. A host gcc exists here, bundled with Qt, and it is not to be used |
| **win11 skyhorizon**, the demo laptop | the cross build and the board. CubeIDE 2.2.0 supplies `arm-none-eabi-gcc` 14.3.1, `cmake` and `ninja`, none of them on PATH until `P01-toolchain-first-light/Use-CubeIDEToolchain.ps1` is dot-sourced. Flashing needs no tool: the probe presents a disk as D: labelled `NOD_H7A3ZIQ` and a `.bin` copied onto it is programmed. The console is COM13 at 115200 | does not author the volume |
| **wsl on skyhorizon**, `bing@JPTOUPM678` | the C syntax and warning check, gcc 15: `python3 python/tools/check_c_syntax.py`. This is the first test of any C change, before CI | has no board attached |

`python/tools/build_host.py` builds the host libraries the suite drives through
`ctypes`. It calls the compiler directly rather than through CMake, which is why
it exists, and it runs on the CI runner and on skyhorizon rather than here.

P01 owns the toolchain file, the startup code and the linker script that every
other project depends on.

## Requirements

Python 3.12 and a C11 and C++17 compiler. Two requirement files, and the split
between them is the point rather than tidiness.

| File | What is in it | Installed by the CI runner |
|---|---|---|
| `requirements.txt` | `pytest`, and nothing else | yes |
| `requirements-hardware.txt` | `pyserial` for a serial link, and `daqhats` noted for the Raspberry Pi only | no |

Nothing in `python/firmkit/` or `python/tests/` imports anything from the second file. If a
module there ever needs one, that module is in the wrong half of the project: it
needs a device, so it belongs in a project directory behind a command the suite
does not call. `P09/listen.py` is the example, and it guards its import and
refuses cleanly rather than failing at the point of use.
