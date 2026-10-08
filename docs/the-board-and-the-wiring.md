# The board, before you wire anything to it

Welcome. This is the long document, and it is deliberately long.

If you have just picked up a NUCLEO-H7A3ZI-Q and you would like to avoid the
particular evening that this repository already spent, you are exactly the
reader this was written for. Everything below was paid for once. You should not
have to pay for it again.

It is organised as the work actually happens, in four parts:

1. **Setting up.** What board is in front of you, which manual is yours, which
   machine does which job, and how to get a byte out of it.
2. **The facts, exhaustively.** Every board and silicon fact this repository has
   established, each one with the authority that settled it and the date it was
   read.
3. **Making decisions.** The choices that followed from those facts, with the
   reasoning and the cost of each, including the ones that look overcautious
   until you see what they caught.
4. **Before wiring up, and reflections on rewiring.** The pre-flight checklist,
   then an honest account of the wiring we changed our minds about, what we
   would rewire next, and what we would leave exactly as it is.

A note on tone before we start. This document is pedantic on purpose. It repeats
pin numbers, connector names, directions and dates rather than saying "as above",
because "as above" is how a 3.3 V output ends up connected to a 5 V rail. If the
repetition feels excessive, that is the intended feel. Please read it as care
rather than as doubt about you.

## How to read a fact in this document

Not all facts are equally strong, and pretending otherwise is how a confident
wrong number gets published. So every fact here carries one of four labels, and
the label is part of the fact.

| Label | What it means | What you may do with it |
|---|---|---|
| **Settled** | Read from a named authority that names *this* board or *this* die, with the date it was read | Write code against it. Wire to it |
| **Corroborated** | Two or more independent sources agree, but none of them is ST | Act on it, and say where it came from. Do not call it a datasheet fact |
| **Derived** | Computed from a settled fact, or read back out of a register | Useful, and weaker than it looks. A read-back proves the bits and not their meaning |
| **Open** | Nobody here has read it | The code refuses rather than guessing. See below for why |

**On the difference between derived and measured**, because it is the distinction
this whole repository turns on. If you configure a phase-locked loop, read every
divider field back, and compute 280 MHz from them, you have a *derived* figure.
Every field could read back correctly while the clock is something else, because
reading `DIVM1` as 4 proves that the bits are 4 and not that the field is a plain
divisor. A figure becomes *measured* only when an instrument that does not share
the clock under test says so. That took until Sunday 4 October 2026 here, and the
answer was not the nominal.

**On why open facts make the code refuse.** It would be easy to put a plausible
value behind an unread register and move on. Please do not, and here is the
concrete reason rather than the principle. If you guess a timer prescaler, the
board produces a clean square wave at the wrong rate, and **a clean square wave
at the wrong rate is indistinguishable from a correct one without an external
witness.** It will look like working firmware in every way you can check from
inside the part. A refusal is noisy, annoying and immediately actionable. A guess
is quiet and can waste a week.

**An invitation.** If you find a disagreement between this document and your
board, your manual or your own measurement, that is not a nuisance, it is the
most valuable thing you can send back. Several entries below exist because an
earlier version of this file was wrong and something external said so. The whole
point of citing a source and a date is that it makes a contradiction cheap to
locate. Please do contradict it.

---

# Part 1. Setting up

## 1.1 Which board is in front of you, exactly

Please check this against the silkscreen before anything else. It takes ten
seconds and it is the single highest-value check in the document.

| Thing | Value | Label |
|---|---|---|
| Board name | NUCLEO-H7A3ZI-Q | Settled |
| Board reference | **MB1363** | Settled |
| Microcontroller | STM32H7A3ZIT6Q | Settled |
| Core | Arm Cortex-M7 | Settled |
| Maximum core frequency | 280 MHz | Settled |
| Form factor | Nucleo-144, with ST Zio and ST morpho headers | Settled |
| Package suffix | **Q**, the switched-mode regulator variant | Settled |

**The `Q` suffix is not cosmetic and this is the first place people lose an
evening.** It selects a different device header, `stm32h7a3xxq.h`, and a
different power configuration. The device macro is `STM32H7A3xxQ` and never the
non-Q form. Choosing the non-Q macro compiles perfectly well and describes a
part whose core is supplied differently from the one on your desk.

## 1.2 Which manual is yours, and which two manuals are not

This is the fact that makes most published material wrong for this part, and it
is worth stating in a form you can put on a sticky note.

| You want | You do **not** want | Why it matters |
|---|---|---|
| **RM0455**, the STM32H7A3 reference manual | RM0433, which is the STM32H743 | The clock tree, the power and regulator configuration and the memory map all differ. Code copied from H743 material does not run slowly. It produces a board that does not boot |
| **UM2408**, which documents MB1363 | UM2407, which documents MB1364, the NUCLEO-H743ZI2 | Pin assignment tables, connector positions. Different board, different tables |

And then one more level of care inside the right manual, which caught us on
Tuesday 6 October 2026:

**UM2408 documents several boards and tabulates each separately.** Table 17,
pages 38 to 41, is "NUCLEO-H745ZI-Q and NUCLEO-H755ZI-Q pin assignments".
Table 18, pages 42 to 45, is this board's. We first read Table 17 by mistake.

Here is why that near miss is worth more to you than the correct answer was:
**the two tables carry the identical row for PB4, so the wrong table would have
given the right answer.** Nothing in the result would have revealed that the
source was wrong. The only defence is to read the caption every time, including
when the number you get looks right.

If you take one habit from this document, take that one. Check the caption.

## 1.3 Which machine does which job

You can do a surprising amount of real work on a machine that cannot compile for
this part at all, and it is worth knowing which is which before you install
anything.

| Job | Where it happens here | Note |
|---|---|---|
| Authoring, host tests, every checker | win11 aquamarine, the authoring laptop | No cross compiler, by rule. Host gcc exists, bundled with Qt, and is not used for this |
| Host C compilation under a strict warning set | WSL on the win11 skyhorizon demo laptop | gcc 15, newer and stricter than the CI runner's |
| Cross build for the target | win11 skyhorizon, PowerShell side | STM32CubeIDE 2.2.0's bundled toolchain |
| Flashing and the console | win11 skyhorizon | The probe is on that machine |

**The one thing that surprises everybody about CubeIDE's toolchain: nothing is on
`PATH`.** STM32CubeIDE 2.2.0 bundles its own `arm-none-eabi-gcc`, `cmake` and
`ninja` in three separate plugin directories whose names carry their own version
numbers, and registers none of them system-wide. So a fresh terminal greets you
with `cmake : The term 'cmake' is not recognized`, which reads exactly like a
broken installation and is not one. Nothing needs installing. Things need
finding.

In this repository the finding is done by a script you **dot-source** rather than
run:

    . .\projects\P01-toolchain-first-light\Use-CubeIDEToolchain.ps1

Please note the leading dot and space. Run without dot-sourcing, it sets `PATH`
in a child scope and loses it on exit, **which looks exactly like having done
nothing at all.** That is a five minute puzzle the first time and ten seconds
every time after, so it is worth meeting here rather than at the terminal.

`STM32_Programmer_CLI.exe` is bundled too, in its own plugin directory, and it is
the flashing route that tells you what it did.

## 1.4 The three ways in, and the honest comparison

| Route | How | What it tells you |
|---|---|---|
| **Mass storage** | The probe presents a removable disk. Copy a `.bin` onto it | Nothing. It is silent by design |
| **Programmer CLI** | `-c port=SWD mode=UR -w <file.bin> 0x08000000 -v` | Program and verify, with a report. **`mode=UR` is not optional here**, see below |
| **Console** | The probe's virtual serial port | Whatever your firmware prints |

The drag-and-drop route needs no tool installed at all, which is genuinely
convenient, and it has one property worth internalising before you rely on it.
**The drive's idle state is `DETAILS.TXT` and `MBED.HTM` alone.** So a vanished
`.bin` and no `FAIL.TXT` is the normal picture after a successful program, and it
is also the normal picture after nothing happened. That state cannot distinguish
a programmed part from a silent one.

So: reach for the programmer CLI the moment a copy to the disk produces no
console output. Do not stare at the disk contents looking for a clue, because
there is not one there to find.

### `mode=UR`, and the failure that looks like a dead board

Settled on the bench Wednesday 7 October 2026, and this is the single most
time-saving line in Part 1. The programmer's **default connect mode attaches to a
running target**, and on this board that fails:

    Error: Unable to read device id from ROM table
    Error: ST-LINK error (DEV_TARGET_RESET_ERR)

reported twice, including its own automatic retry at 8 MHz. `mode=UR` holds the
reset line low while attaching and then it works first time.

**Read that failure carefully before you suspect the board**, because it is
unusually informative about what is *not* wrong. In that same output the
programmer still reports the probe's serial number, its firmware version, the
board name as NUCLEO-H7A3ZI-Q, and a measured **3.28 V**. So USB, the probe and
board power are all known good, and the only thing failing is the debug port
attach. One caution on that voltage: it is the probe measuring the board's
supply rail, and it is **not** evidence that the core is executing.

**Leave `-rst` off whenever something needs to read the console.** An image that
prints once and stops is over in well under a second, so a reset at programming
time sends the whole report before a console can be opened. Program, then open
the console, then press the black RESET button.

### Four facts the attach reports, from this silicon

Each of these was previously known here only from a machine-readable source
rather than from the part in hand. Read Wednesday 7 October 2026:

    Device ID    0x480
    Revision ID  Rev X
    Device name  STM32H7A/B
    NVM size     2 MBytes
    Device CPU   Cortex-M7
    BL Version   0x92

**The NVM size confirms the 2048 kB of flash in section 2.2 from the hardware**,
which until now rested on two device trees agreeing.

And one note so the revision is not mistaken for a new open question. ST's
converter code has a `HAL_GetREVID() <= REV_ID_Y` branch, and it sits in the
`#else` that this part never compiles, so **there is no revision fork for the
H7A3.** Reading Rev X confirms that rather than complicating it.

On this bench the disk is `D:`, labelled `NOD_H7A3ZIQ`. That label is worth a
pedantic footnote, because it looks like a typo and is not: **a FAT volume label
is eleven characters, `NOD_H7A3ZIQ` is exactly eleven, so ST dropped the E from
`NODE` and kept the Q.** If you are matching the label in a script, match the
real one.

## 1.5 The console is silent until you raise DTR and RTS

This one cost three console runs and was very nearly blamed on the wiring, so
please accept it as a gift.

A `System.IO.Ports.SerialPort` opened with its defaults reads **zero bytes
forever** on this probe's virtual COM port. The port is not broken, the firmware
is not silent, and the cable is fine. Set `DtrEnable` and `RtsEnable` to true
*before* `Open()` and everything arrives, including output buffered from a
previous boot.

Two further details that turn a working reader into a trustworthy one:

- **Print a marker as it counts**, so that a quiet port is distinguishable from a
  script that was not actually running. Those two look identical otherwise, and
  you will hit it.
- **Open the console first, then press the black RESET button while it counts.**
  An application that prints and returns is over in well under a second, so a
  console opened afterwards sees nothing at all.

And one property of our own report, discovered twice before it was believed: the
full report takes about ten seconds to go out of the port, and **a second RESET
press inside that window loses the second half of it.** Two separate captures
lost their core frequency line that way. If a capture looks truncated, suspect
the second press before you suspect the firmware.

---

# Part 2. The facts, exhaustively

Every row below carries its label from the table in "How to read a fact". Where a
date appears, that is when the source was read.

## 2.1 What is on the board

| Thing | Value | Label and authority |
|---|---|---|
| LD1, green | **PB0** | Settled. Two independent machine-readable sources naming this exact board, read Wednesday 30 September 2026, and the board has since printed it |
| LD2, yellow | **PE1** | Settled, same sources. See the warning below |
| LD3, red | **PB14** | Settled, same sources |
| User button | **PC13** | Settled, same sources, confirmed on hardware |
| Ethernet | **None.** No controller, no media access controller, no interface | Settled. Any tutorial pairing this family with an Ethernet network stack is for a different part |
| 32.768 kHz crystal | **Fitted.** The real-time clock is usable | Settled, and since exercised as a measurement reference |
| High-speed clock | From the on-board debugger, **bypass mode**, nominal 8 MHz | Settled that it is bypass. The frequency is nominal and is **not** 8 MHz: see 2.6 |
| Virtual COM port | USART3 on **PD8 and PD9**, alternate function 7, 115200 8N1 | Settled Friday 2 October 2026 from ST's own board support package for this board, and the board has printed through it since |

**One pedantic warning about LD2, because ST's own file disagrees with itself.**
The board support package defines `LED2` twice, on PE1 and on PB7, in the same
file. The branch that applies to this board is the PE1 branch, which is the LD2
yellow this repository has settled. If you grep for `LED2` and take the first
hit, you have a coin flip. Read which branch you are in.

**On the console pins and the word "settled".** USART3 as the peripheral was
never in doubt. PD8 and PD9 were, for two days, because they are the Nucleo-144
convention and conventions are not board facts. What settled them was ST's own
board support package naming `COM1_TX_PIN` as `GPIO_PIN_8` on `GPIOD` and
`COM1_RX_PIN` as `GPIO_PIN_9` on `GPIOD`. Zephyr's documentation for this exact
part agrees, and so does the board, which has printed thousands of lines through
them. That is about as corroborated as a fact gets.

## 2.2 The memory map, to the kilobyte

Both machine-readable sources agree exactly. Base addresses are architectural;
the **lengths** are what differ between family members, and these are this
part's.

| Region | Base | Size | What to know |
|---|---|---|---|
| Flash | `0x0800 0000` | 2048 kB | Dual bank. The split is a design choice, not a fixed boundary |
| DTCM | `0x2000 0000` | 128 kB | Zero wait state, the fastest memory on the part. **The main transfer engines cannot reach it at all** |
| AXI SRAM | `0x2400 0000` | 1024 kB | Three contiguous banks: 256 kB, then 384 kB at `0x2404 0000`, then 384 kB at `0x240A 0000` |
| Second domain SRAM | `0x3000 0000` | 128 kB | Two banks of 64 kB, the second at `0x3001 0000` |
| Low-power domain SRAM | `0x3800 0000` | 32 kB | Survives while the other domains sleep, which is why the third transfer engine exists |

**The DTCM trap, stated plainly because it fails in the worst possible way.** Put
a buffer in DTCM, point a main transfer engine at it, and you do not get an
error. You get a transfer that never completes. The engine is not reporting a bad
address, it simply cannot see that memory. This is why the linker script in this
repository places its transfer-engine buffers deliberately **outside** DTCM and
says so at the region.

**And one piece of arithmetic worth being fussy about.** The declared SRAM adds
up to **1312 kB**. The commonly repeated "about 1.4 MB" is only reached by
including the tightly coupled instruction memory, whose size neither source
declares. Please state it as 1312 kB plus that memory rather than rounding up to
a number no source gives. With 1312 kB this part sits at the large end of the
small machine-learning envelope, so memory is almost never your binding
constraint. Throughput and energy are.

The transfer request multiplexers sit at `0x4002 0800` and `0x5802 5800`, both
with 107 requests.

## 2.3 Peripheral inventory

From the upstream device list. Counts, because the counts are what constrain a
design.

- **Two 32-bit timers and fifteen 16-bit.**
- Six serial peripheral interfaces, four two-wire interfaces, five universal
  synchronous and five asynchronous receivers.
- Two controller area network interfaces with flexible data rate.
- Two 16-bit analogue-to-digital converters, 24 channels, 3.6 MSPS. One 12-bit
  digital-to-analogue converter.
- A random number generator. Sixteen transfer channels. A graphics accelerator, a
  still-image codec and a display controller.
- **No cryptographic accelerator**, which matches expectation for this part.

**Only two 32-bit timers exist, and that is a budget rather than a detail.** If an
instrument spends one, a later project cannot have both that instrument and a
32-bit application timer. This repository came close to spending one and then
did not, which is in Part 5.

**A correction worth carrying, because the usual summary gets it backwards in
both directions.** The Cortex-M7 **does** have the signal-processing instruction
extension, so optimised neural kernels do help on this part, through that path.
What it lacks is the newer vector extension, which is where the impressive vendor
speed-up figures come from. Please do not let a document imply either that this
core has no acceleration or that it has the fast kind.

## 2.4 The clock tree, and the eight steps that raise it

The factorisation first, because there are two ways to reach 280 MHz from 8 MHz
and they are **not** interchangeable.

    ST's, and the one this repository uses:
        8 MHz / 4  = 2 MHz PLL input,  times 280,  divided by 2  = 280 MHz
        PLLM 4, PLLN 280, PLLP 2, paired with the 2 to 4 MHz input range

    The obvious one, which this repository wrote down first and which is wrong
    in company:
        8 MHz / 2  = 4 MHz PLL input,  times 140,  divided by 2  = 280 MHz

Both reach 280 MHz. Here is the trap: **the PLL input range field must contain
the actual input**, and ST pairs its choice with the 2 to 4 MHz range constant.
Implement the second factorisation while copying ST's range constant and you have
configured a range that does not contain its own input. The arithmetic is right
and the configuration is wrong.

**Two asymmetries in the divider fields**, both settled from how ST's own code
reads them back rather than from symmetry:

- `DIVM1` holds **the value itself**.
- `N1`, `P1`, `Q1` and `R1` hold **the value minus one**.

Getting `N1` off by one gives 279 MHz rather than 280. That is 0.36 per cent:
too small to notice and too large to put in a timing table.

### The eight steps, in the order they must happen

| Step | What it does | Why it is here |
|---|---|---|
| 1 | **Select the supply**, exiting Run* mode | Nothing else works without it. See below |
| 2 | Voltage scale, reset scale to **scale 1** | Scale 0 is not reachable directly |
| 3 | Voltage scale, **scale 1 to scale 0** | The only legal route to the top scale |
| 4 | **Flash wait states**, up to 6 | Before the frequency, never after |
| 5 | The debugger's 8 MHz, **in bypass** | `HSEBYP` before `HSEON`: there is no crystal to start |
| 6 | PLL configured and **locked**, core still on the internal oscillator | |
| 7 | Bus prescalers, while the system clock is still 64 MHz | |
| 8 | **Switch** the system clock to the PLL, last and gated on `SWS` | |

**Why that order, and not a cheaper one.** Every intermediate state is one the
part can run at 64 MHz. More voltage than needed is not a hazard. More wait
states than needed is slow and correct. Either one *short* is fatal after the
switch. So the ordering costs nothing and buys the property that matters: **on
any failure the sequence returns without switching the system clock**, leaving
the part at 64 MHz with a working console and the fault readable on the terminal
rather than inferred from a board that stopped talking.

Four of five bring-up attempts on Sunday 4 October 2026 produced a readable
diagnosis instead of a dead board, and that was the design rather than luck.

### Two things no field description implies

These two are the reason the first two attempts failed, and neither is
discoverable by reading about the `VOS` field however carefully.

**At reset this part has no supply selected.** It is in Run* mode with both
`SMPSEN` and `LDOEN` set in `PWR_CR3`. That is the *absence* of a selection
rather than a selection, and in that state the regulator **declines every voltage
scale change in silence.** Not an error. It simply never reports ready. Exiting
Run* mode is one write choosing one supply, and **it locks until the next power
cycle.**

**Voltage scale 0 is reachable only from scale 1.** The part boots at scale 3.
Writing scale 0 straight over it is not a permitted transition, and again the
part does not report that as an error, it declines to become ready. ST's own
documentation says so in a doc comment above the scaling macro, which is where
we eventually found it, and not in the field description.

**And now the uncomfortable consequence, recorded rather than smoothed over.**
ST's own NUCLEO-H7A3ZI-Q example writes scale 0 directly from reset and then
waits with `while(!VOSRDY){}`. On this board that bit does not set. **That loop
does not terminate.** The bounded wait in this repository's `clock280.c` is the
only reason the first attempt came back as a diagnosis rather than as an image
that stopped, and the design note arguing for bounding every wait was written
before there was any evidence it would be needed.

So: please bound your waits. Not because ST's example is careless, but because an
unbounded wait on a ready flag turns "this transition is not permitted" into
"the board is dead", and those need very different responses from you.

### This board's core is supplied through the SMPS

Settled two ways, and the second cost a power cycle.

Every CubeIDE project file under `Projects/NUCLEO-H7A3ZI-Q` defines
`USE_PWR_DIRECT_SMPS_SUPPLY`, and that branch of ST's supply configuration
clears `LDOEN` and leaves the switched-mode supply running.

We wrote the opposite first, clearing `SMPSEN` and setting `LDOEN`. The board
printed its banner, reached that instruction and stopped: no step report, no
second banner, nothing further. A power cycle recovered it completely and the
64 MHz image ran unchanged afterwards. **Nothing was damaged.** But the core on
this board is supplied through the SMPS, so removing the SMPS removed the supply.

Part 5 has the full reflection on why that reasoning failed, because the
reasoning is more transferable than the answer.

### One deliberate departure from ST, with its cost named

ST sets `HPRE` to divide by one, so the AHB runs at 280 MHz, then divides each of
APB1, APB2, APB3 and APB4 by two. That needs four prescaler fields and this
repository has confirmed one.

Leaving the other three at their reset divide-by-one while the system clock is
280 MHz **would run three buses at 280 MHz against a 140 MHz limit, with nothing
in the part to refuse it.** So this repository takes the other route to the same
core frequency: `CDCPRE` divides by one so the CPU gets 280 MHz, and `HPRE`
divides by two so the AHB and every APB below it sits at 140 MHz.

The cost is explicit: **half of ST's AHB bandwidth to flash and the AXI SRAMs.**
The condition for recovering it is also explicit: read the other three prescaler
fields from the same header. That is a half hour of reading nobody has needed
yet, and when you need it, that is where to start.

And a precedent found later, worth knowing before you decide: ST's own LPTIM
pulse counter example for this board describes its configuration as the CPU at
280 MHz, both domains' AHB at 280 MHz and every APB at 280 over 2. So **the AHB
at 280 MHz is permitted at voltage scale 0 with six flash wait states.** Neither
choice is wrong. Ours is the conservative one and the precedent for the other is
on record.

## 2.5 What the clock actually measures as

This is the part of the document that most changes how you should quote a number
from this board, so it gets the space.

Everything below was measured against **this board's own 32.768 kHz crystal**,
over a one second gate, by counting core cycles against 256 ticks of the
real-time clock's sub-second register. The crystal does not come from the PLL
chain, which is the whole reason it can witness it.

### The core, at the 280 MHz setting

Ten readings between Sunday 4 October 2026 and Tuesday 6 October 2026, always
below nominal, spanning about 1037 parts per million with no trend:

    279672822 Hz   1168 ppm low
    279435368 Hz   2017 ppm low
    279714764 Hz   1019 ppm low
    279541148 Hz   1639 ppm low
    279692180 Hz   1099 ppm low
    279424340 Hz   2056 ppm low
    ... and further readings inside that span

### The internal oscillator, nominal 64 MHz

Always **above** nominal. Across the whole record of readings it runs from
**2630 to 3257 parts per million high**, which is a spread of about 627 ppm.

**And the history of that figure is the reason this section exists, so it is
reported rather than tidied.** Over the first ten readings the spread was
**357 parts per million**, and it held at 357 across five, seven, nine and ten
readings, to the hertz each time. For a day it was the one figure here that had
survived every test put to it, and this document would have told you so.

**It did not survive.** Later readings widened it to about 627 ppm. That is the
fourth claim about this board's clocks to be stated and then withdrawn by the
next observation, after drift, after temperature, and after the core's own bound.

**And on Thursday 8 October 2026 a completely different instrument landed inside
that band**, which is the first independent corroboration any clock figure here
has had.

P06's converter samples on a TIM6 trigger whose divisors were computed for exactly
64 MHz, so the sampling rate carries this oscillator's error directly. Counting
blocks of 64 conversions against the **host PC's clock** over 698.831 seconds, with
700 800 conversions in one unbroken stretch, gave **1002.818 Hz, which is +2818
parts per million.**

| instrument | timebase it trusts | what it measured | result |
|---|---|---|---|
| the on-board gated counter | the real-time clock crystal | this oscillator, nominal 64 MHz | 2630 to 3257 ppm high |
| a host console script | the PC's clock | P06's sampling rate, nominal 1000 Hz | +2818 ppm high |

**Neither instrument is the oscillator under test, and they agree.** If the
divisors assume exactly 64 MHz, the band above requires a rate between 1002.630
and 1003.257 Hz, and 1002.818 Hz sits in it. The sign was predicted by the
opposite-sign table further down: a requested delay comes out short at the reset
clock, so a requested 1000 microsecond period is short and the rate is high.

**Two things are worth taking from that, and the second is the uncomfortable one.**
A figure measured one way and confirmed another way is on much firmer ground than
anything else in this section, so the 2630 to 3257 ppm band has earned more trust
than it had. And **the error is now known to propagate into a deliverable**: P06
claims exactly 1000.0 Hz within 0.1 per cent, and this oscillator misses that by a
factor of about 2.8 on its own. That is not a firmware defect and it cannot be
fixed in firmware.

**And the obvious escape does not work, which the record above already said.** The
first response was to propose running that clock tree from the 8 MHz bypass instead,
on the grounds that it is crystal-referenced. **It is not.** This document's own
`HSE_HZ_BYPASS` evidence puts that source 1019 to 2016 parts per million low with a
scatter of 998, against the internal oscillator's 627, so it is the worse of the two
on stability as well as on offset. The only source on this board with crystal
accuracy is the 32.768 kHz crystal every figure here is measured against, and 32768
does not divide to 1000. It divides to exactly 1024.

Please read the pattern rather than the number. Four times in a row, a spread
measured over the readings in hand was quoted as though it were a property of the
part, and four times the next reading widened it. The explanation arrived only
when the clock was measured twice **inside one run**, which is the next section
but one, and it accounts for all four at once.

### So the 8 MHz from the debugger is not 8 MHz

The PLL dividers are integers and the fractional term is disabled, so the system
clock is the input times 35. Dividing the measured cores by 35 puts the
debugger's clock output at about **7983868 to 7991850 Hz**, roughly 1000 to 2000
parts per million low, and moving.

**Four independent arguments that this is the external clock and not our
reference**, because one observation is never a mechanism:

1. Both clocks in a run are gated by the same crystal, so a drifting crystal
   moves both readings by the same relative amount. Between runs the internal
   oscillator moved 119 ppm while the PLL moved 849 ppm.
2. **The relative movement changes sign.** Between successive pairs of runs the
   external clock moved against the internal one by -730, +1102, -473 and -204
   parts per million. **A figure that changes sign twice cannot be the shared
   reference drifting.**
3. The internal oscillator's spread is smaller than the core's, so whatever
   scatters is not the gate. **This argument has weakened and is kept with its
   weakening stated**, because that is more useful than quietly dropping it: the
   ratio of the two spreads was 4.5 when it was first made, then 2.8, and on the
   full record it is about 1.65. It is still the right direction and it no longer
   carries much weight on its own. Argument 2 is what carries this now.
4. A crystal fast enough to explain the PLL result would have read near
   64100000 Hz on the internal oscillator. It reads near 64194318 Hz. **Excluded
   by a factor of three.**

### And how the crystal itself was checked

A measurement is only as good as its reference, so: the same crystal gate was
used to measure the internal oscillator, which two other instruments had already
measured at 64.17 to 64.18 MHz across six reductions against a host PC. The
crystal says 64194318 Hz, agreeing with them to between 223 and 379 parts per
million.

**One claim was withdrawn here and the withdrawal matters.** An earlier version
of this repository's comments said a 32.768 kHz crystal is good to a few tens of
parts per million, as though this bench had shown it. It had not. That is the
datasheet expectation. What has been demonstrated is that the crystal and a host
PC's clock agree to a few hundred parts per million, and which of them is the
better reference is not something this bench can establish. A few hundred is
enough to fix the sign and size of a 1168 ppm effect and not enough to quote its
last digit.

### The finding that governs everything else: the clock wanders

On Tuesday 6 October 2026 the clock was measured **twice inside one run**, and
then as a row of readings inside a single window. The result:

**990 parts per million inside one 24-second window, against 1671 parts per
million across sixteen separate runs.**

Nearly all of the between-run spread is present *within* a single window. The
shape is **wander**, not drift and not a step. So the honest description of the
spread figures above is not "reproducibility" and not "a bound", it is **what
this bench has been seen to span**.

The practical rule that follows, and please apply it to anything you derive from
this clock: **nothing derived from this clock is good to better than a part in a
thousand, however carefully it is measured.** Seven candidate explanations for
the wander have been declined here, three of them after being stated and then
refuted by the next observation. We offer no mechanism. We offer the bound.

### The opposite-sign table, which hides a real trap

The two clocks err in **opposite directions**, and a single sentence about "about
0.117 per cent" hid that for a while:

|  | reset clock | 280 MHz setting |
|---|---|---|
| `board_core_hz()` reports | 64000000 | 280000000 |
| measured | about 64194318 | about 279672822 |
| the reported **frequency** is | 3027 ppm low | 1170 ppm high |
| a measured **duration** comes out | 3036 ppm long | 1168 ppm short |
| a requested **delay** comes out | 3027 ppm short | 1170 ppm long |

**The trap:** a figure taken at 64 MHz and the same figure taken at 280 MHz carry
errors of **opposite sign**, so comparing them across the clock change produces a
4200 part per million artefact **that looks like a real effect of raising the
clock.** If you benchmark across that boundary, this is the correction you need.

Nothing published in this volume quotes a time to better than a part in a
thousand, so nothing already published is wrong. The table is for the next figure
that wants to be.

### What the nominals are, and why they did not change

`CORE_HZ_TARGET` stays 280000000 and `HSE_HZ_BYPASS` stays 8000000.

That is deliberate. The measured figures are this board, this probe, one
afternoon, against a reference whose own accuracy is not traceable. Substituting
them would fit the code to one sample **while reading as more precise than it
is.** The nominal is the honest constant; the measurements are evidence about how
good the nominal is. They are recorded as a table of observations that a new
reading extends, and nothing computes from them.

There is a small history here worth repeating as a caution. For a few hours on
Sunday 4 October 2026 this repository carried `CORE_HZ_MEASURED` as nine digits.
When the second run disagreed it became `CORE_HZ_MEASURED_1` and `_2`. The third
run made that shape obviously wrong. If you find yourself numbering a constant
after the run that produced it, the thing you have is a table, not a constant.

## 2.6 Pins that are settled, with their connector positions

| Pin | Function | Connector position | Label and authority |
|---|---|---|---|
| **PB0** | LD1 green | | Settled |
| **PE1** | LD2 yellow | | Settled |
| **PB14** | LD3 red | | Settled |
| **PC13** | User button | | Settled, confirmed on hardware |
| **PD8** | USART3 TX, console, AF7 | | Settled, ST board support package |
| **PD9** | USART3 RX, console, AF7 | | Settled, ST board support package |
| **PD12** | LPTIM1_IN1, counting input, **AF1** | **CN10 pin 21**, Arduino D29 | Settled. ST's LPTIM pulse counter example and ST's PWM external clock example both state the CN10 position |
| **PE13** | TIM1 channel 3, signal source, **AF1** | **CN10 pin 10**, Arduino D3 | Settled. `Examples/TIM/TIM_DMA/readme.txt` gives peripheral, channel, port pin, connector, pin number and Arduino name in one sentence |
| **PB4** | Marker pin for the witness | **CN7 pin 19**, Arduino D25 | Settled. UM2408 Rev 6 **Table 18, page 44**, signal name D25, function SPI_B_MISO on SPI3 |
| PA13 | SWDIO | | Debugger. Do not touch |
| PA14 | SWCLK | | Debugger. Do not touch |
| PB3 | SWO | | Debugger trace. UM2408 gives it as D23/SWO |

**On "settled" for the CN10 positions.** Both come from ST readme files for this
exact board, which is the strongest authority this repository has used for a
connector position. Note that the **Arduino names are weaker than the pin
numbers**: CN10 pin 21 comes from ST, while D29 comes from the STM32 Arduino
core's variant index and no ST file for this board names PD12 as D29. **Wiring
needs the pin number**, so the pin number is the load-bearing fact and the
Arduino name is a convenience.

**And ST disagrees with itself once, in the material we were reading.** Those
readmes give twelve further port-pin to Arduino-number pairs, and the STM32
Arduino core agrees with twelve of the thirteen. The thirteenth is ST against ST:
one file says "CLK Pin: PA5 (CN07.D10)" and another says "PA5 : CN7.D13". Zephyr
and the Arduino core both say D13, and on an Arduino Uno R3 header D13 is the
clock while D10 is the chip select, so D13 is right and the D10 is a slip.
Nothing here depends on PA5. The point is only this: **these readmes are not
individually authoritative.** Two of them agreeing is worth much more than one of
them stating.

**One thing UM2408 does not say about PB4, so it is not claimed here.** Its
footnote list gives PB3 as D23/SWO and says nothing about PB4. Whether PB4
carries a JTAG function by default on this package is a datasheet question
nobody here has read. It does not affect the debugger in use, because the probe
runs SWD, which needs SWCLK on PA14, SWDIO on PA13 and SWO on PB3, and none of
those is PB4.

## 2.7 One register address worth singling out

`RCC_APB2ENR` is at offset **0x150**, and **not** the 0xF0 that ST's own trailing
comment claims.

This repository took 0x150 by walking the register structure in declaration order
rather than by reading the trailing comments, **because those comments are the
STM32H743's from `RSR` onward.** The H743's reserved gap is smaller, so every
offset after that point in the comments is 0x60 light.

That was a derivation rather than a reading, and on Tuesday 6 October 2026 it was
**confirmed on hardware**, by an argument worth following because it generalises:

> A wrong address means `TIM1EN` is written somewhere else, so TIM1 stays
> unclocked, so writes to its registers do not stick. The signal source reads
> five of its registers back before it enables the output. It returned ok at both
> clocks with every field holding what was written. **Therefore the clock enable
> reached TIM1**, therefore the address is right.

That is how you promote a derived address to a confirmed one without an
oscilloscope: find a consequence that can only hold if the address is correct.

## 2.8 The analogue converter, brought up and sampling

Confirmed on the board on Wednesday 7 October 2026, each by a read-back rather
than by reading a manual. P06's timer back end took the converter through five of
its eight steps, then through all eight, and by the end of that evening it was
sampling at a nominal 1 kHz. **The rate itself is not measured and this section
says so at length**, because the last subsection is the one worth reading.

| Fact | Evidence |
|---|---|
| `DEEPPWD` is bit 29 and **set at reset** | The control register reads `20000000` as found, before anything touches it |
| `ADVREGEN` is bit 28 | Written and read back `10000000` |
| `ADCAL` is bit 31 and self clearing | Written `80000000`, and the register reads back with it gone. **The poll count varies and was first recorded as though it did not:** 180 on the runs of Wednesday 7 October 2026 and **187** on Thursday 8 October 2026. The bit self clears, which is the fact; how long it takes is an observation |
| `BOOST` is two bits at bit 8 | A boost setting of 1 leaves the control register at `10000100`, the regulator bit still beside it |
| `CKMODE` field value 3 is divide by four, at bit 16 in the **common** block | Wrote 3, read back `00030000` |
| `RCC_AHB1ENR_ADC12EN` is the right bus clock enable | Nothing above would have stuck without it |
| `ADRDY` is bit 0 of the interrupt and status register | The ready report read `00001001` |
| `EOCIE` is bit 2 and `OVRIE` is bit 4 of the interrupt enable register | Wrote `00000010` for `OVRIE` alone and read back `00000014`, which is both, with `EOCIE` already set from the step before. Thursday 8 October 2026 |
| `OVRMOD` reads **0 as found**, so an overrun PRESERVES the data register and discards the new conversion | Printed as found rather than written, Thursday 8 October 2026. This is the bit that decides which samples a gap contains, and nothing here chooses it |

**Two of those deserve a sentence more than a row.**

The regulator **accepted a direct write from reset**, which was worth watching
for because the voltage scaling in 2.4 does the opposite: scale 0 is reachable
only from scale 1 and the regulator declines a direct write **in silence**. The
analogous trap was looked for here and did not occur, and an absence somebody
tested for is worth recording.

And `DEEPPWD` reading 1 as found is what lets this document say deep power-down
was **exited** rather than merely clear afterwards. An earlier run printed only
what it wrote, which was equally consistent with the bit having been clear
already, and the as-found line was added to separate the two. The prediction that
it would read 1 was written down before the flash.

**One bit was unexplained, was named rather than guessed, and was read on
Thursday 8 October 2026.** The status register read `00001001`, so besides
`ADRDY` at bit 0 something at bit 12 was set.

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

### The enable erratum does not bite here, and the experiment that settled it

ST's `stm32h7xx_hal_adc.c` lines 3713 to 3716 carry an erratum workaround in its
own words: if `ADEN` is set less than four ADC clock cycles after the `ADCAL`
bit, continue setting `ADEN` until `ADRDY` becomes 1. So the obvious
implementation, one write then a poll, looks like the wrong one.

**The first run reported four passes of the re-asserting loop, and that was read
as the erratum biting. It showed no such thing.** The loop writes `ADEN` and
polls `ADRDY` on every pass, so four passes fits two stories equally well: the
first three writes were ignored and the fourth took, or the first write took and
the flag simply needed four passes to rise. Nothing in that report separated
them.

**So the image was changed to try one write first**, with the prediction and its
reasoning written down before the flash. The condition is *less than four ADC
clock cycles* after `ADCAL`, and two `printf` calls sit between the calibration
step and the enable: about fifty bytes at 115200 baud is some four milliseconds,
which at this image's 16 MHz ADC kernel clock is roughly sixty four thousand ADC
cycles. The window closes many thousands of times over before `ADEN` is written,
so the condition cannot be met by this code at all.

**The board agreed, and the number is what makes it conclusive:**

    adc enable, one write      ok       wrote 00000001  read back 00001001
        ready after 4 polls with NO re-assertion, so the erratum did not bite here

Four polls, which is **the same count the re-asserting loop reported.** Had the
re-assertions mattered, the single write would have taken longer or refused. They
contributed nothing, and the four passes were always the flag rising.

**Scope this claim carefully, because it is narrower than "the erratum is
wrong".** What is established is that this image does not meet the condition, so
the workaround is unnecessary *here*. ST's rule stands for code that does meet
it, which is any sequence putting `ADEN` within four ADC clock cycles of
`ADCAL`.

**And that is exactly why the loop is kept.** It runs only when the single write
was not enough, so it costs nothing in this image, and it covers the case a later
version reintroduces by dropping the console output or moving the enable next to
the calibration. The comment in `acq_timer.c` says so, so the protection is not
deleted as dead code by somebody who measures only this build.

### The bring-up completed, and then produced nothing, Wednesday 7 October 2026

Steps 6, 7 and 8 were written the same evening and `acq_start` returned 0 for the
first time in this project's history. **All eight steps reported ok, every
register read back what was written, and not one sample arrived.** The console
stopped mid-word at the instant TIM6 was enabled and the board said nothing for
twenty seconds.

That is the most instructive thing that has happened to this repository, so it is
recorded in full rather than as a fixed bug.

**The last open converter fact came off the list without the datasheet.** The
channel is **13, on PC3, at CN9 pin 5 and Morpho CN11 pin 37**, stated twice in
ST's own `ADC_DualModeInterleaved` example for the NUCLEO-H7A3ZI-Q, once in its
`readme.txt` and once in its `main.h`. Stated twice by the same authority, for
this board, is why it is **settled** rather than corroborated, and
`stm32h7a3zi.pdf` stays unread. Three more field values were read rather than
inferred from bit counts: `EXTEN` 1 for the rising edge from
`LL_ADC_REG_TRIG_EXT_RISING` being `EXTEN_0` alone, `RES` 0 for sixteen bits, and
`SMP` 7 for 810.5 cycles. The sampling time is the one **choice** in that set,
because ST's five examples for this board disagree about it, and it is recorded
as a choice with its arithmetic rather than copied from whichever file opened
first.

**Then three causes, in the order they were found, and the order matters.**

Two writes had been missing from `acq_timer.c` since the day it was written, and
both only became reachable once the bring-up stopped refusing. `EOCIE` was never
set in the converter's own `IER`, so the end of conversion set a flag and nothing
else happened: `NVIC_EnableIRQ` tells the controller to accept a line, it does
not tell the peripheral to raise one. And `ADSTART` was never set, so a trigger
arriving at an unarmed converter did nothing.

**Adding both was necessary and was not sufficient**, and the commit that added
them said otherwise. The third cause was that **the vector table had no device
interrupt entries at all.** `g_vectors` in `c/board/startup.c` ended at
`SysTick_Handler`, sixteen architectural entries. `ADC_IRQHandler` existed, was
declared weak, compiled and linked, and was simply not in the table, so the first
end of conversion fetched a vector from past the end of the array and branched
into whatever the linker had placed after `.isr_vector`.

**Three things have to be true for a handler to run, and only two of them read
back:** the peripheral's own enable in its `IER`, the controller's enable through
NVIC, and a vector for the handler to reach. The third has nothing to read back
from, which is precisely why it was the one that went missing while every step
report said ok. This is the same shape as 2.4's silent regulator and 1.2's wrong
manual: the failure that leaves no trace is the one the method has to be built
around.

**And the comment sitting in that table had predicted it**, which is why this is
recorded as a prediction coming true rather than as a defect found. It said the
positions were to be confirmed and that leaving them out would be worse, because
an interrupt that fires with no entry runs into whatever the linker put next.
That is exactly what happened. The prediction was written down and never acted
on, and the cost was one flash.

The positions are now sourced for this exact part, from `IRQn_Type` in
`stm32h7a3xxq.h` of STM32Cube_FW_H7_V1.13.0: `ADC_IRQn` 18, `USART3_IRQn` 39,
`TIM6_DAC_IRQn` 54, and the table index is 16 plus the number. They are written as
designated initialisers so that **only positions this repository has actually read
are filled**, which keeps the original discipline rather than abandoning it. An
unconfirmed position still cannot silently misroute, because it is not written at
all. RM0455 would say the same and remains unread; it is not needed for this.

### It samples, and the rate is still not measured

With the vector installed the same image sampled. FLASH 14 192 bytes,
`.isr_vector` 64 bytes to 224, and twenty six report lines over roughly twenty
five seconds:

    tim6 started, so conversions begin on its update event
    seq 15  blocks 15  mean 4  overruns 0
    ...
    seq 390  blocks 390  mean 3  overruns 0

390 blocks of 64 conversions, 24 960 conversions, `overruns` 0 throughout. **The
whole chain runs:** TIM6's update event, TRGO, a conversion started by hardware
with the processor out of the timing path, end of conversion, the controller, the
vector, the handler, the block, and the application draining it.

**Three counters that do not read each other agree**, which is double entry on a
running system rather than on a hand table. `seq` increments in the handler,
`blocks` increments in `main`, and `overruns` increments in the handler when a
block becomes ready while the previous one is uncollected. A single lost block
moves all three at once. `seq` equals `blocks` on all twenty six lines and
`overruns` never left zero.

**Now the part that is easy to skip, and the reason this section exists.** None of
that is a rate measurement, and the project's own claim is about rate.

Half the prediction published before the flash **could not have failed.** It said
`blocks` would rise by roughly 15 each time, and `main.c` reports when
`blocks - last_report` reaches `ACQ_RATE_HZ / count`, which is 1000 over 64,
which is 15. That increment is arithmetic in the source and could not have come
out otherwise whatever the board did. The comment above that line claimed the
reporting cadence does not itself depend on the thing being measured, which is
false for the cadence in time and is corrected in the same commit. The rule that
an experiment must be able to fail carries over to a prediction, and this one was
half exempt.

The only rate observation available is the **wall-clock cadence of the lines**,
read against a PowerShell loop printing a marker every ten iterations of a 500
millisecond sleep. Between two markers a nominal 20 seconds apart, `seq` went
from 60 to 390: 330 blocks, 21 120 conversions, which is 1056 Hz. **That is an
upper bound rather than an estimate**, because each loop iteration costs its sleep
plus its own work, so the elapsed time was longer than 20 seconds and the true
rate is below the figure. The direction of the bias is known and its size is not,
which is a fair description of not an instrument. So 1056 Hz is **consistent with
1000 Hz and is not evidence for it.**

None of `MEASUREMENT.md`'s four criteria is touched by it, because all four are
about the **instants** at which conversions happened and nothing in this capture
observed an instant.

**And the board cannot settle it in principle, not merely in practice.** Any
timebase on this die that could count the sampling interval is derived from the
same oscillator that defines it, so firmware timing itself would confirm a wrong
rate exactly as readily as a right one. That is the property from the
introduction, met again at the last step, and it is why the external witness was
designed before any of the firmware. The MCC 118 on the Raspberry Pi watching the
marker on PB4 at CN7 pin 19, which 2.6 sourced from UM2408 Table 18, is the
measurement. **The wire is not connected**, and until it is, the 1 kHz in the
project's title is a claim.

The converted value is not the result either. `mean` reads 3 to 5 of 65 535 on a
**floating PC3**, said in advance to be meaningless. One thing in it earns a
sentence: it **varies between reports**, where a register never written or a
converter returning a constant would read the same every line. That is weak
positive evidence that these are real conversions, weak because it cannot
distinguish a floating input from a grounded one.

### The gap that was named, and closed the next day

**The capture above reported `overruns 0` for 390 blocks and that number could not
have been anything else.** `OVRIE` was not enabled and the converter's own `OVR`
was not read at all, so a run with converter overruns would have looked exactly as
clean as that one did. The gap was named in the file and in this document rather
than left for a reader to find, which is the right thing to do with a hole and is
not the same as not having one.

**On Thursday 8 October 2026 it was closed for the timer back end**, and the two
numbers are now reported side by side because they answer different questions and
either can be zero while the other is not. `acq_overruns()` counts blocks the
**application** never collected, which is `main` being too slow. The new
`convovr` counts conversions the **converter** could not deliver because the
previous result was still in its data register, which is the interrupt being too
slow or not arriving. A rising `convovr` with `overruns` at zero would mean the
handler is losing samples the application never hears about, and nothing in the
old report could have shown that.

**Enabling the interrupt was only half of it, and the halves had to ship
together.** With `OVRMOD` clear the data register is preserved and the new
conversion is discarded, and `OVR` stays set, so an overrun does not lose one
sample: the converter keeps discarding until the flag is cleared. The handler
therefore counts `OVR` and clears it, which makes the count a real count and lets
the stream recover. Enabling a line whose flag nobody clears would have held the
part in the handler, which is a worse failure than the one being fixed.

**`OVRMOD` is reported as found and not set**, following the as-found discipline
that 2.8 adopted after the deep power-down step could not say whether a bit had
been cleared or had never been set. The bit decides what an overrun does to the
data, so a reader who has the count but not the bit cannot say which samples a gap
contains. Neither value is obviously right for this application, and choosing one
would be a configuration decision presented as a fact, so the value is printed and
the choice is left for somebody who has the number.

**The other two back ends return -1 from the new call rather than 0**, and the
interface makes that distinction impossible to lose: 0 means the flag was watched
and never set, -1 means nobody looked. The `dma` build is where that answer is
least comfortable, because a transfer engine reading the data register is exactly
the arrangement in which `OVR` is the primary failure mode rather than a remote
one. It is the build that most needs the flag and the one least entitled to report
it, since it does not configure the converter at all yet.

## 2.9 What is still open, and treated as a question

Please do not fill any of these in from a sibling part. Each one is refused in
code rather than guessed.

**The one with a safety note attached, which is why nothing here touches option
bytes:** can a bad option byte write leave the board unrecoverable without
external tooling? Until somebody answers that, no project in this repository
writes option bytes, and the note goes before the steps rather than after.

Still open, in rough order of how soon you are likely to want them:

- The three remaining bus prescaler fields, `CDPPRE3` and the two domain-4
  fields, which is what would let the AHB run at 280 MHz.
- The `TIMPRE` boundary in RM0455. The timer clock is a multiple of the APB clock
  once that prescaler divides by anything; this repository answers only the
  divide-by-one case, where both `TIMPRE` values agree, and refuses the rest.
- **Which converter channel reaches which other pin on this package**, and the
  highest channel number. This is what is left of a row that used to hold five
  items and is the first entry here to close by two different routes on one day.
  The clock mode came off it by being **decided** rather than read: it was never a
  datasheet fact, it is a configuration choice ST also leaves to the caller, and
  P06 takes synchronous with `CKMODE` dividing by four with its reasoning and its
  cost recorded. The channel, its sampling time, the resolution and the trigger
  edge field then came off it by being **read from ST's own example and LL header
  for this board**, which is why `stm32h7a3zi.pdf` is still unread and the
  converter samples anyway. What remains is the general mapping, which only a
  project needing a different pin will want, and the converter arithmetic still
  caps its channel at 19 as a conservative choice pending that document.
- Which pin carries TIM2_ETR or TIM5_ETR on this package. No example in ST's pack
  configures either, which is why the 32-bit counter route stayed closed.
- The maximum reliable baud rate on the virtual COM port. 921600 is asserted
  elsewhere and is not read from MB1363 here.
- Which jumper carries the current measurement, exactly what it isolates, and
  whether the debugger's own draw is excluded.
- Whether trace output on PB3 is routed to the on-board debugger at all, and
  whether that probe does trace. If not, a serial-backed event ring is the only
  option.
- Whether the board has a user USB connector, of what type, and whether that
  peripheral is usable at the same time as the debugger.
- Whether the backup domain works without a coin cell, and therefore whether a
  crash record survives a power cycle.
- The remaining Arduino header mappings: which pins carry the two-wire interface,
  which serial peripheral instance is on D11 to D13, which asynchronous receiver
  is on D0 and D1, which analogue pins reach which converter channels.
- Which transfer engine reaches which memory, in full. Expected: the main engines
  cannot reach the tightly coupled memories, a master engine reaches everything,
  and a third reaches only one small memory but survives deep sleep.
- Flash write granularity, the write-once error-correction rule, and whether one
  bank can be read while the other is written.
- Whether the hardware checksum unit exists here and how configurable it is:
  polynomial, initial value, input and output reversal.
- Cache line size, and any errata on store buffering or cache maintenance for
  this revision.
- Which low-power modes exist and what each retains.
- Whether the independent watchdog survives the deep sleep modes, the debug
  freeze bits, and the documented silent failure where the debug clock must be
  enabled before the freeze register write takes effect.

**Three facts that moved off this list, as encouragement.** Whether the cycle
counter increments with no debug probe attached used to be open; the wrapper now
enables it, checks that it moved, and reports which backend it got, so the
question is answered per boot rather than assumed. And "the timer registers the
frequency counter needs" was on this list for days, with a note saying that was
why no frequency here was measured. Both halves stopped being true, the second
before the first: the core clock has been measured against the crystal since
Sunday 4 October 2026.

And the converter bring-up sequence, which sat on this list as the one item
gating all three of P06's back ends, came off it in a single evening on
Wednesday 7 October 2026 and the converter now samples. **It was answered from
ST's pack rather than from the document the list said it needed**, which is the
most practically useful thing in this whole file: a question filed as a
datasheet question turned out to be answerable from headers and examples that
were already on the disk. Before assuming an open item here is blocked, grep
the pack for it.

---

# Part 3. Making decisions

Facts do not tell you what to build. These are the decisions that followed from
Part 2, each with its reasoning and its cost, because a decision presented
without its cost is advocacy rather than engineering.

Several of these look overcautious on first read. Each one is followed by what it
actually caught, so you can judge for yourself whether it would be worth it on
your bench.

## 3.1 Where a value is unconfirmed, refuse rather than return a plausible number

**The decision.** Every function whose correctness depends on an unread register
returns an error code instead of a value. Not a default, not a fallback, not a
best guess.

**Why.** Because of the property in the introduction: a guessed timer prescaler
produces a clean square wave at the wrong rate, and that is indistinguishable
from a correct one from inside the part. The failure is silent, plausible and
durable.

**The cost, stated honestly.** For two days this board could not blink an LED
beyond first light, because the clock tree was unconfirmed and everything timed
depended on it. That is a real cost and it was the right one. The part boots on
its internal oscillator, so an LED *can* blink using only settled facts, and that
is exactly where the first project stops.

**What it caught.** The checksum path refuses without falling back to the
software reference, deliberately, **because a silent fallback would hide a
misconfigured peripheral behind a correct answer.** That is the whole decision in
one sentence.

## 3.2 Bound every wait, and report the register value when it runs out

**The decision.** No `while(!flag){}` anywhere. Every wait is bounded in **polls**
rather than in time, because time is often the thing being measured, and running
out is a reported failure with the register's value attached.

**Why.** See 2.4. ST's own example for this board waits on a bit that does not set
from the reset state on this part.

**What it caught.** All three of the first bring-up failures. Each came back as a
line naming the step, what was written, what read back and how many polls it
waited, instead of as a board that stopped talking. The design note arguing for
this was written **before** there was any evidence it would be needed, which is
the nicest kind of note to find in your own file.

## 3.3 Read every register back, and print both values

**The decision.** Every step records what the code *intended* and what the
register *holds*, and prints both.

**Why.** "The clock did not come up" is not actionable. "The flash latency wrote
6 and read back 0" is. And the three failure modes are distinguishable by what
comes back:

| What read back | What it usually means |
|---|---|
| The reset value | A wrong peripheral base address |
| Zero | A reserved field, so the field moved |
| Neither | A field that moved between parts |

**The cost.** A longer console report. About ten seconds of it, which turned out
to matter once: see the note in 1.5 about a second RESET press losing the second
half.

## 3.4 Gate the switch on the hardware's answer, not on the request

**The decision.** The system clock switch is gated on `SWS`, the status field,
and never on `SW`, the request field. Likewise the voltage scale is checked
through `ACTVOS`, the scale **actually in use**, and not only `VOS`, the scale
**selected**.

**Why.** A selected value and an active value that disagree is a *different fault*
from a write that did not land, and `ACTVOS` is the only field that separates
them. That field was added to the report after the first failure, specifically
because the first failure could not be diagnosed without it.

## 3.5 Measure against something that does not share the clock

**The decision.** The measurement gate is the 32.768 kHz crystal, read through the
real-time clock's sub-second register, and the comparison is the core's own cycle
counter.

**Why the real-time clock rather than a timer.** The crystal has to be *observed*
for a cycle count to be gated on it. A timer with the crystal as a capture source
needs its input selection, capture, compare and control registers. The sub-second
register needs the clock selected and then only reads. **Five register groups
fewer, no write protection key, and nothing inside the real-time clock is written
at all**, so the calendar is never touched.

**Four things that had to be sourced first, each with the failure it would have
caused:**

| Sourced | What it prevents |
|---|---|
| `RCC_APB4ENR`'s `RTCAPBEN` gates bus access to every real-time clock register, and **without it they read zero rather than refusing** | A zero sub-second register never changes, so the gate times out and the diagnosis points at the crystal |
| The address is derived from two already-pinned bases rather than recalled | A wrong base reads plausible values from somewhere else |
| This part has `RTC_ICSR` at offset 0x0C with the synchronisation flag at bit 5. **The STM32H743 has `RTC_ISR` there with a different layout** | Almost all STM32H7 material is written against the H743 |
| `PREDIV_A` is **read** from the prescaler register rather than assumed to be its reset value | Assuming it is exactly how a gate ends up wrong by a factor while every other number on the console looks right |

**And one write that is refused rather than forced.** The clock source selection
can be set once after a backup domain reset, and changing an existing selection
needs a backup domain reset, **which would stop the crystal the measurement
depends on.** So a selection that is already the crystal is accepted, anything
else is refused, and the register is reported either way. The backup domain
survives a system reset, so the second run of the image finds its own earlier
selection.

## 3.6 Choose the instrument with a complete set of facts, not the better one

**The decision.** The frequency counter counts on **LPTIM1**, a 16-bit
peripheral, rather than on TIM2 or TIM5, which are the two 32-bit timers.

**Why, and this is a nice one.** The 32-bit route is genuinely better: more range,
fewer wraps. But it needs a pin carrying TIM2_ETR or TIM5_ETR, and **no example in
ST's pack configures either on this package.** The LPTIM route has ST's own
working pulse counter example for this exact board, naming the peripheral, the
counting mode, the pin and its alternate function.

So: **the narrower instrument with a complete set of facts beat the wider one
with a gap in it.** That is the refuse-rather-than-guess rule applied to a design
choice rather than to a value.

**An unexpected benefit.** Only two 32-bit timers exist on this part (2.3), and
spending one on an instrument would have meant a later operating-system project
could not have both the instrument and a 32-bit application timer. Between the
gate moving to the crystal and the counter moving to LPTIM1, **this instrument
costs that project no 32-bit timer at all.**

**The cost, and it is a real one you must respect.** An LPTIM **samples** its
input rather than counting edges asynchronously, so it has a ceiling, and the
ceiling is a configuration choice rather than a property of the part: about
32 kHz on the internal low-speed oscillator, far higher on an APB clock. Three
consequences:

1. **The ceiling must be printed beside every measurement**, because a signal
   above it does not read low. It reads wrong by however many edges were missed,
   and nothing in the count says so.
2. The kernel clock choice does **not** affect accuracy, because the count is a
   number of edges and the gate is the crystal. A PLL-derived kernel clock costs
   range, not correctness. Please keep those two separate; conflating them is
   easy and leads to the wrong worry.
3. 1 kHz, which is what the sampling project asks of this instrument, is three
   decades below even the low-speed ceiling.

## 3.7 Put arithmetic that reads no register in its own file

**The decision.** Four files now hold pure calculation and dereference nothing:
the clock tree decode, the frequency arithmetic, the timer arithmetic and the
converter arithmetic.

**Why.** Because the alternative is untestable. A divide that lives inside a file
which dereferences a peripheral can only be checked by flashing the board and
believing the number, and **an arithmetic error that only a flash can find is an
arithmetic error nobody will find.**

**What it caught, concretely.** The frequency arithmetic has cases for four
distinct risks, and the first of them is the one worth quoting: getting multiply
and divide the wrong way round gives 768000 millihertz instead of 1000000 at the
frequency the sampling project cares about. **That is low by 23 per cent rather
than obviously broken.** Low by 23 per cent is exactly the kind of wrong that
survives a glance at a console.

**And the hand-written expected values are double entered**, meaning each one is
computed a second time by a different route. That has found three errors in this
repository's hand tables, including one case that claimed 995136186 millihertz
where the definition gives 996108. **Three orders of magnitude out**, in a table
written to check something else.

## 3.8 Publish the threshold before taking the measurement

**The decision.** Before a measurement is taken, write down what the possible
answers mean, in a form that can be wrong.

**Why.** So the answer cannot be interpreted to fit. A method written after the
first plot is a rationalisation of whatever the plot showed.

**A worked example.** Before the first clock measurement, both outcomes were
computed and committed: a difference inside a few hundred parts per million
confirms the derivation including every field's interpretation, and a difference
near **3572 parts per million** means the multiplier is off by one, giving 279 MHz
rather than 280. That is a thousand times the measurement's own noise. When the
board answered 1168 ppm low, there was nothing to decide.

**Used four times on Tuesday 6 October 2026**, and the last use returned exactly
the figure named in advance.

## 3.9 Test the analysis on synthetic input, including input it must refuse

**The decision.** Every analysis is proven on constructed data whose answers are
known, and **the cases it must refuse are the half that matters.**

**The example that justifies the whole practice.** The sampling project's witness
has nine synthetic captures. The one that earns its place is the second: a clean
wave at **1100 Hz**. Every implementation must fail it on the rate and pass it on
the jitter. The analysis measures 1099.989 Hz with a spread of 2.8 microseconds,
so it is clean **and it is wrong**. A witness that called that a pass would be
worthless, and four implementations agreeing on a wrong answer would still be
wrong.

**And then test the tests, by introducing defects on purpose.** This found real
defects every time it was done here, including a retry limit off by one because a
counter named "attempts" was incremented on failure. **A suite that has never
been shown to fail is a suite nobody has checked.**

## 3.10 Find one claim an outside authority can settle

**The decision.** Where possible, include one test that is not self-referential.

**Why.** Two implementations agreeing proves that they agree. A published check
value proves they are right. The framing project's checksum reaching the
published `0x29B1` over the nine bytes `123456789` is the only test in that
project that appeals to anything outside this repository, and it runs first.

---

# Part 4. Before you wire anything up

This is the part to read standing at the bench, and it is short on purpose.

## 4.1 The two safety rules, in full, every time

**Rule one. Ground first, before any signal wire.** Not optional and not last.
Without a common return your witness measures an undefined potential, so the
reading is not merely inaccurate, it is meaningless.

**Rule two. 3.3 V logic only.** Nothing from a Raspberry Pi or a HAT is ever
driven back into a board pin. A marker is an **output on the board** and an
**input on the witness**, and it stays that way for the whole session.

These two are restated in full in every wiring table below rather than
cross-referenced, which is the pedantry this document promised.

## 4.2 The pin budget: what is already spoken for

Before you choose a pin for anything, please check it against this list. Every
one of these is settled and in use.

| Pin | Taken by |
|---|---|
| PB0 | LD1 green |
| PE1 | LD2 yellow |
| PB14 | LD3 red |
| PC13 | User button |
| PD8, PD9 | USART3 console |
| PD12 | LPTIM1_IN1, the counting input |
| PE13 | TIM1 channel 3, the signal source |
| PB4 | The marker pin for the witness |
| PA13, PA14, PB3 | SWDIO, SWCLK, SWO. The debugger |

**What "free" has to mean when you pick the next one.** PB4's labelled function
in UM2408 is `SPI_B_MISO` on SPI3, and it is still free, because that is an
**alternate function the pin can take** rather than a board function: nothing on
the board drives it. So the test is not "does the table name a function", it is
"does anything on the board drive it, and does it reach a header".

## 4.3 The self test wire, stated in full

This is the wire that turned the frequency counter from a configured peripheral
into an instrument.

> **One jumper wire, on the win11 skyhorizon demo laptop's board.** From
> **CN10 pin 10**, which is **PE13**, which is Arduino **D3**, which is **TIM1
> channel 3 output**, an **output from the board**; to **CN10 pin 21**, which is
> **PD12**, which is Arduino **D29**, which is **LPTIM1_IN1**, an **input to the
> board**. Both ends are on **CN10**, the same connector, so this is a single
> jumper across one header rather than a lead between two.

No ground wire is needed for this one, and it is worth saying why rather than
leaving it as an exception: both ends are pins on the same part, so they already
share a return. The ground-first rule applies the moment a *second instrument*
is involved, which is 4.4.

**Why both ends on one connector is more than a convenience.** It removes the
single most likely wiring error, which is a lead landing on the wrong connector's
pin of the same number. CN7 pin 21 and CN10 pin 21 both exist.

**Keep the frequency well under 16777216 Hz**, which is this instrument's usable
maximum as configured. The self test runs at 1 MHz, comfortably inside it.

## 4.4 The marker wire to the witness, stated in full

> **Two wires, and the ground goes in first.**
>
> **Wire one, ground, in first, always.** From a **GND** pin on the
> NUCLEO-H7A3ZI-Q to the **ground terminal** on the MCC 118 DAQ HAT's screw
> terminal block, which is on a **Raspberry Pi 4**.
>
> **Wire two, signal.** From **CN7 pin 19**, which is **PB4**, which is Arduino
> **D25**, the **marker pin, an output from the board**; to **MCC 118 channel 0**,
> an **input to the HAT**.

**A series resistor in the signal wire is insurance against the wire finding the
wrong terminal, not protection for the board**, which is driving that pin
deliberately. Worth including, and worth being clear about what it is for.

**One convenience that makes this project survive a missing part.** Every header
on this bench is female and nothing can be soldered, so most wiring needs
male-to-male jumper wires. The MCC 118 uses **screw terminals that accept a bare
wire end**, so this particular measurement works either way.

**Still open against the HAT's user guide:** its input range and whether it
offers an external trigger. Every range it is likely to offer contains 3.3 V,
which is a reason to *expect* it to be fine and **not** a reason to skip reading
the guide.

## 4.5 The pre-flight list, before power on

Please run down this list. It is nine items and it takes under a minute.

1. **The silkscreen says MB1363.** Not MB1364. Ten seconds, highest value.
2. **The device macro is `STM32H7A3xxQ`**, with the Q.
3. **The manual open on your screen is RM0455**, and if it is a board question,
   **UM2408**, and if it is a pin table, **Table 18** and not Table 17.
4. **Ground wire in first**, if a second instrument is involved.
5. **Every signal direction checked once more**, out of the board and into the
   witness, never the reverse.
6. **Only one shield fitted**, if any. Never two: address collisions and the
   3.3 V budget. Note that the STWIN.box is a USB-C device, not a shield and not
   a HAT.
7. **The console is open before you press RESET**, with DTR and RTS raised, and
   it prints a marker so you can tell a quiet port from a script that is not
   running.
8. **One RESET press**, then wait about ten seconds for the full report. A second
   press inside that window loses the second half.
9. **Record `git describe` beside whatever you capture.** A capture with no build
   identity is not evidence, and you will not reconstruct it later.

## 4.6 A note on the supply write, because it is the one you cannot retry

The supply selection in step 1 of the clock sequence is **the single write in the
sequence that cannot be retried without removing power.** It locks until the next
power cycle.

The file says so at the line where it happens, and this document says so here,
because knowing it *before* the first attempt changes what you do when the board
goes quiet: **reach for the power, not the RESET button.** We did that by
instinct at the time and only later found the reason, which is in 5.6.

---

# Part 5. Reflections on rewiring: what changed, and what did not

This is the part that is hardest to write and most useful to read, so it is the
longest. Four wiring arrangements were designed and then abandoned before any
wire went in, one was abandoned after it went in, and one register write was
simply wrong. Each is kept here with its reasoning, because the reasoning
transfers and the answer does not.

## 5.1 The self test that was a feedback loop

**What was proposed.** ST's `LPTIM_PWMExternalClock` example for this board puts
LPTIM1_OUT on PD13 and LPTIM1_IN1 on PD12. **Adjacent pins.** One short wire
between them looks like the obvious self test, and it is irresistibly tidy.

**Why it was abandoned, and the example's own name says it.** In that
configuration **the counter is clocked by IN1 and the compare generates the PWM
on OUT**, so the output is derived from the input. Wiring one to the other makes
a feedback loop, and **counting a signal that was generated from the counter
under test proves nothing at all.** It would have produced a confident number
with no information in it.

**The honest part.** This was proposed twice before the problem was noticed. Not
once. The tidiness of two adjacent pins is genuinely seductive, and the error is
not visible in the pin numbers, only in what the peripheral does with them. If
you find yourself delighted by how convenient a wiring arrangement is, that is a
good moment to ask what is generating the signal you are about to measure.

## 5.2 The signal source that was the green LED

**What was proposed.** `Examples_LL/TIM/TIM_PWMOutput` is the obvious source for a
known frequency. It drives TIM3 channel 3 on **PB0** at alternate function 2, and
its code is directly usable.

**Why it was abandoned.** **PB0 is LD1**, the green LED. It has been a settled
board fact here since Friday 2 October 2026, and it is the indicator the clock
image blinks as part of its own evidence. That example is unusable here however
convenient its code.

**The transferable lesson.** An ST example's pin choice is a property of the
example, not of your board's free pins. Check every pin an example uses against
your own pin budget (4.2) before you take anything else from it. This is a thirty
second check that saves a confusing debugging session where your LED flickers and
your measurement is nonsense.

## 5.3 The source that was chosen, and why it is better than a self test

**What was chosen.** **TIM1 channel 3 on PE13**, from
`Projects/NUCLEO-H7A3ZI-Q/Examples/TIM/TIM_DMA`. It collides with nothing already
claimed: not LD1 on PB0, not LD2 on PE1, not LD3 on PB14, not the button on PC13,
not the console on PD8 and PD9, not the counting input on PD12.

**Why this is better than a self test rather than merely adequate**, which is the
part worth keeping. A general-purpose timer's output comes from its APB clock and
therefore from the PLL. ST's own example makes that explicit by computing its
prescaler from the system core clock. So counting it against the crystal gate
**compares the PLL to the crystal through a path that shares no component with
the cycle-counter measurement**, which counts core cycles inside the part. Two
instruments, one quantity, no common component but the crystal.

**And it can fail informatively**, which is what makes it an experiment rather
than a demonstration. Agreement confirms the crystal gate and the cycle counter
against each other. **Disagreement says which kind of wrong by its size:**

| Size of disagreement | What it points at |
|---|---|
| A ratio near a small integer | A divider misread |
| A few hundred parts per million | The crystal |
| Moves between runs | The debugger's 8 MHz, already known to move by a part in a thousand |

Please notice that this table was written **before** the wire went in. That is
3.8 applied to a wiring experiment.

## 5.4 Two connectors became one, and it mattered more than expected

**What changed.** For a while the two pins were known only by their Arduino names,
D3 and D29, from Zephyr and the STM32 Arduino core. That settled the question that
was actually blocking the work, **which was whether a wire between them was
possible at all**, and it left the connector and the position unread.

Then ST's own readme files gave the positions: **CN10 pin 10 and CN10 pin 21.**
Both on one connector.

**Why that is more than a convenience.** It collapses the most likely wiring
error. With two connectors in play, a lead landing on the right pin number of the
wrong connector is a real and quiet mistake. With both ends on CN10 it cannot
happen.

**And a reflection on source strength that is worth more than the pin numbers.**
Zephyr and the STM32 Arduino core were written independently and **agree exactly
on the one pin both of them cover.** That agreement is what made the index for the
other pin credible enough to act on. Neither is ST. When ST's readmes later gave
the positions, they confirmed the Arduino numbering **from a third source** and
also showed ST disagreeing with itself about PA5 (see 2.6). The lesson is not
"trust ST" or "trust the community". It is: **two independent sources agreeing on
an overlap is a stronger instrument than one source asserting.**

## 5.5 The instrument that could never have done the job

**What was believed.** For days, the frequency counter was named in this
repository's own files as the instrument that would settle whether the core
really runs at 280 MHz.

**Why that was wrong, and it is wrong by construction rather than by degree.** The
frequency counter counts an **external** signal against the **internal** timer
clock. If the internal clock is wrong, the measured external frequency is wrong
**by the same factor, and nothing shows.** It cannot witness the clock it is
clocked by. No amount of care in the implementation could have fixed that.

**What was there all along.** The 32.768 kHz crystal had been sitting in this
repository's board header as a settled fact since Friday 2 October 2026, **without
ever being switched on.** It is independent of the internal oscillator, the
external clock and the PLL.

**And the reflection worth carrying.** The right instrument was already a
documented fact in our own file, and the wrong instrument was named in three
places including a `printf` that the board was actively printing. One of those
corrections was made in a file's opening comment and the `printf` twenty lines
below it was missed, so **the console contradicted itself within twenty lines**:
it printed "this is still not a measurement" and then measured the clock against
the crystal immediately underneath.

If you correct a claim, please grep for it. Correcting the comment and leaving the
output is a very easy thing to do, and the output is the half your reader sees.

## 5.6 The gate that moved from a timer to the crystal, before the file existed

**What changed.** The frequency counter's recorded design spent a **second** 32-bit
timer on the gate, clocked from the core. On the afternoon of Sunday 4 October
2026, before any of that code existed, the design changed: the gate became the
crystal.

**Why, and the reasoning is purely arithmetic.** A core-clocked gate would carry
the core's error into **every** frequency that instrument ever reports, and carry a
**different** error at each of the two clocks the board runs at. The core had just
been measured at about 1168 parts per million from nominal, and that figure moves.
So a core-clocked gate is an instrument whose calibration depends on which clock
you happen to be running.

The crystal gate was already written, for the cycle-count measurement. So the
change **removed the second timer entirely**, made the counter crystal-accurate
rather than PLL-accurate, and shortened the list of unknowns.

**The reflection.** A measurement changed the design of a file that did not yet
exist. That is the best possible time for a design to change, and it only happened
because the measurement was taken before the convenient implementation was
written. If the counter had been built first, the gate would have been a timer,
the instrument would have inherited a moving error, and the error would have been
very hard to see from the inside.

## 5.7 The one that was mine, and it stopped the part

**What happened.** The supply step was written to clear `SMPSEN` and set `LDOEN`,
selecting the low-dropout regulator. The board printed its banner, reached that
instruction and stopped. No step report, no second banner, nothing further. A
power cycle recovered it completely and the 64 MHz image ran unchanged afterwards
with the button and both clocks reporting as before. **Nothing was damaged.**

**Why I got it wrong, because the reasoning is the transferable part.** I had found
one conditional branch in ST's source that selected the LDO, treated the existence
of that branch as evidence about **this board**, and backed it with an argument
that felt solid: the LDO is on the die, so selecting it cannot remove the supply.

**That argument is true about the die and says nothing about the board.** A supply
is a board fact. ST sets it **per project**, in `.cproject`, not in any header,
which is also why a header search found nothing.

**The rule this repository already had covers it**, which is the uncomfortable
part: a value that is not confirmed against a named source makes the code refuse
rather than guess. I guessed, on **the one register in the sequence whose wrong
value can stop the part rather than report a refusal**, and the design's own
safety property did not help, because the part lost power rather than failing a
gate.

Both the answer and the wrong argument are recorded in the register header,
dated, **because "the LDO is on the die so it must be safe" is the kind of
plausible reasoning that will recur.** It will probably recur for you too, in some
other register. The shape to watch for is an argument about the silicon being
offered as evidence about the board.

**And two independent sources now agree**, which is worth more than either alone:
ST's project configuration for this exact board, and the board's own behaviour
when the opposite was written. The second is the only piece of evidence in the
whole sequence that came from hardware rather than from a file, and it cost one
power cycle.

## 5.8 Why a reset was the wrong recovery, discovered afterwards

**What was observed.** Running the same image twice, once after a power cycle and
once after the black RESET button, gave **different starting states**:

    after power is removed   PWR_CR3 00000006, LDOEN 1 SMPSEN 1, ACTVOSRDY 0,
                             HSEBYP 0, and the supply step took 31 polls
    after the RESET pin      PWR_CR3 00010004, LDOEN 0 SMPSEN 1, ACTVOSRDY 1,
                             HSEBYP 1, and the supply step took 0 polls

**The supply selection, the regulator's ready state and the external clock's
bypass bit all survive a system reset.** Only removing power restores them.

**Two things follow.** The supply step is safely idempotent: writing the selection
it already holds is a no-op that still passes its own read-back gate, which ST's
own supply function has an explicit branch for. And **it explains why the earlier
failures needed power removed rather than the RESET button.** A locked supply
selection is not cleared by a reset, so a wrong choice stays wrong until the board
loses power.

**The reflection, and it is a small one about honesty.** The power cycle was done
by instinct at the time. It is now a reason. Those are different things and the
record distinguishes them, because "we did the right thing" and "we knew why"
should not be allowed to blur together after the fact.

**One documentation consequence.** The register dump's heading used to say "the
registers at reset" and printed whatever it found, which after a button press is
not a reset value at all. **A reader comparing that dump against a reset-value
column in a manual would find disagreements that are not faults.** It says "as
found" now.

## 5.9 What we would rewire next, and what we would leave alone

**Would rewire, in this order:**

1. **Nothing about the self test.** It is one jumper on one connector between two
   pins that collide with nothing, and it compares two instruments sharing no
   component but the crystal. We would build it again exactly as it is.
2. **The marker wire to the witness** is next to go in, and the only open question
   is the HAT's input range, which is a document to read rather than a wire to
   change.
3. **The three unread bus prescaler fields**, which is reading rather than
   rewiring, and would let the AHB run at 280 MHz instead of 140. ST's own example
   is the precedent that it is permitted at voltage scale 0 with six wait states.
   This is the clearest piece of value sitting behind half an hour of manual
   reading.

**Would leave alone, with reasons:**

- **The buses at 140 MHz**, until those three fields are actually read. The cost is
  bandwidth. The alternative risk is three buses at double their limit with
  nothing in the part to refuse it, and that asymmetry is not close.
- **The 16-bit counter**, even though the 32-bit one is better, until a pin
  carrying TIM2_ETR or TIM5_ETR on this package is sourced. And note that the
  32-bit route has become *less* attractive, not more: the gate moving to the
  crystal removed the second timer, so the wider counter would now cost a 32-bit
  timer that a later project wants.
- **Option bytes**, entirely, until somebody answers whether a bad write can leave
  the board unrecoverable without external tooling. That question is cheap to
  answer and expensive to get wrong, which is exactly the shape of thing to answer
  before touching rather than after.
- **The nominal constants.** 280000000 and 8000000 stay, with the measurements
  beside them as observations. See 2.5.

## 5.10 A short, honest tally

Over four days of bring-up on this board, between Saturday 3 October 2026 and
Tuesday 6 October 2026:

- **Three traps were avoided by reading ST's headers before writing anything**:
  the reversed voltage scale encoding, the divider asymmetry, and the
  factorisation with its input range.
- **Two were found by the board**, because nothing in any field description
  implies them: the scale 0 transition rule and the supply selection.
- **One was mine**, and it stopped the part: selecting the LDO.
- **Four wiring arrangements were designed and abandoned** before any wire went
  in, and that is the cheapest possible place to abandon one.
- **Four claims about the clock's behaviour were stated and then withdrawn** when
  the next observation refuted them: drift, then temperature, then the core's
  bound, then the internal oscillator's 357 parts per million. A fifth, a
  cold-against-warm pattern, was named and declined on the spot rather than
  claimed. All five are kept on the page beside their refutations, because a
  reader who sees only the final sentence learns a number, and a reader who sees
  all five learns why the number is stated the way it is.
- **Every one of those four was the same mistake**, which is the part worth
  taking away: a spread measured over the readings in hand, quoted as though it
  were a property of the part. The fix was not more readings. It was measuring
  twice inside one run, which answered all four at once.

If that tally reads as a lot of errors, consider the alternative framing: every
one of them is written down, dated, with its reasoning, and none of them is in a
published figure. The board now agrees with a crystal to **0.6 parts per
million**. Getting there required being wrong in public eleven times, and the
record of being wrong is the part that makes the 0.6 believable.

---

# Appendix A. The eleven family-versus-part traps, collected

This repository exists to say that material for the STM32H7 **family** is not
material for this **part**. Here are the eleven instances found so far, in one
place, because seeing them together is more convincing than meeting them one at
a time.

| # | The trap |
|---|---|
| 1 | The **Q suffix** has its own device header, `stm32h7a3xxq.h`. The non-Q macro compiles and describes a different power configuration |
| 2 | `PWR_REGULATOR_VOLTAGE_SCALE0` is `0b11` on this part and `0b00` on the H743, with scale 3 reversed to match. **Writing it from H743 knowledge selects the lowest performance scale while believing it selected the highest**, then runs 280 MHz outside the regulator's range. Nothing refuses |
| 3 | The RCC register structure's **trailing offset comments are the H743's** from `RSR` onward, a difference of 0x60, because the H743's reserved gap is smaller |
| 4 | `RCC_APB2ENR` is at **0x150**, not the 0xF0 that comment claims. Confirmed on hardware Tuesday 6 October 2026 |
| 5 | This part has `RTC_ICSR` at offset 0x0C with its flag at bit 5. The H743 has `RTC_ISR` there with a **different layout** |
| 6 | `PWR_FLAG_VOSRDY` resolves to a **different register** on the H743 lines than on this part's |
| 7 | The peripheral bases are offsets from two-domain names, `CD_` and `SRD_`. The H743 has three domains and calls them `D1_`, `D2_`, `D3_`, and **the correspondence is not a rename**: what the H743 calls `D3_AHB1` this part calls `SRD_AHB4`. So H743 code does not compute a wrong address, **it fails to resolve the symbol at all**, which is the kindest possible failure and worth knowing |
| 8 | **UM2407 documents MB1364**, the NUCLEO-H743ZI2. **UM2408 documents MB1363**, this board. Four places in three files here had it wrong |
| 9 | Inside UM2408, **Table 17 is the H745 and H755** and Table 18 is this board, **and both carry the identical PB4 row**, so the wrong table gives the right answer |
| 10 | The timer clock prescaler macro's documentation **names the H743's field spelling**, where this part has a different one |
| 11 | The analogue converter's channel preselection register and its boost rule both sit **behind preprocessor conditions on silicon version**, so ST's own code does two different things depending on which part it is compiled for |

Five of those eleven were found on Tuesday 6 October 2026 alone. If you are
working on this part and you have not yet met one, you probably will this week.

# Appendix B. Where each fact lives in this repository

So you can check any claim above against the code rather than trusting this
document.

| What | Where |
|---|---|
| The settled and open lists, as an interface contract | `c/board/board.h` |
| Every register address, with the authority and date for each | `c/board/stm32h7a3_regs.h` |
| The clock measurements, run by run, with their caveats | `c/board/stm32h7a3_regs.h` |
| The eight-step clock sequence | `c/board/clock280.c` |
| The memory regions, with the transfer-engine trap at the region | `c/ld/stm32h7a3zi.ld` |
| The marker pin, with its UM2408 citation and the near miss | `projects/P06-timer-sampling/c/marker.c` |
| The eight-step converter bring-up, every value with its source or its reason | `projects/P06-timer-sampling/c/acq_timer.c` |
| The three interrupt positions, with their citation and what their absence cost | `c/board/stm32h7a3_regs.h` |
| The vector table, and the comment whose prediction came true | `c/board/startup.c` |
| The counting input and the counter's ceiling rule | `c/instr/freqcount.h` |
| The crystal reference and its gate | `c/instr/lseref.h` |
| Arithmetic with no register access, host tested | `c/clock/clocktree.c`, `c/instr/freqmath.c`, `c/instr/pwmmath.c`, `c/instr/adcmath.c` |
| Building, flashing and reading the console | `docs/building.md` |
| What each project claims and in which state | `projects/README.md` |

---

# Appendix C. The five documents this one depends on

Deliberately five, and not a bibliography. Every document below is one that a
claim in this file actually rests on, or one that an **open** item in 2.9 is
waiting for. If a vendor document is not here, no claim here needs it, and the
last section of this appendix says where the rest live.

The **Status** column is the part to read. Several items in this document are open
precisely because a page has not been read yet, and on Tuesday 6 October 2026
st.com would not serve a PDF to the authoring laptop at all, by `WebFetch` or by
`curl`, with or without a browser user agent. So "unread" below is a real state
with a real cause, not an oversight.

| Document | What this file uses it for | Status |
|---|---|---|
| **UM2408**, STM32H7 Nucleo-144 boards, **MB1363** [link](https://www.st.com/resource/en/user_manual/um2408-stm32h7-nucleo144-boards-mb1363-stmicroelectronics.pdf) | The board itself. **Table 18, page 44** gives PB4 as CN7 pin 19, signal D25, function SPI_B_MISO on SPI3, which settles the marker pin. **Page 37** gives CN7, CN8, CN9 and CN10 as female Zio connectors on the top side, which is how CN7 is known to be a Zio header | **Read** Tuesday 6 October 2026, those two pages |
| **RM0455**, STM32H7A3, H7B3 and H7B0 reference manual [link](https://www.st.com/resource/en/reference_manual/rm0455-stm32h7a3b3-and-stm32h7b0-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) | The authority this whole repository defers to. Named in 1.2 as the manual that makes H743 material wrong here. **Two open items in 2.9 are waiting on it**: the `TIMPRE` boundary and the three unread bus prescaler fields. The converter came off this list entirely on Wednesday 7 October 2026: all eight bring-up steps run on the board and it samples, every value sourced from ST pack headers and board examples rather than from the manual. The interrupt positions went the same way, read from `IRQn_Type` for this exact part rather than from this manual's vector table | **Largely unread.** Register facts here came from ST's device headers and board examples instead, which is why so much of 2.9 is still open |
| **STM32H7A3ZI datasheet** [link](https://www.st.com/resource/en/datasheet/stm32h7a3zi.pdf) | The authority for the questions that are datasheet questions rather than manual questions: whether **PB4** carries a JTAG function by default on this package (2.6), the general mapping of converter channels to pins, the highest channel number, flash write granularity and the cache line size. **It no longer blocks P06.** The four items that did, on Wednesday 7 October 2026, were the channel, its sampling time, the resolution and the trigger edge, and all four were answered from ST's `ADC_DualModeInterleaved` example for this board and its LL header instead | **Still unread, and the converter samples anyway**, which is the useful finding: the pack said what the datasheet would have. The converter arithmetic still caps its channel at 19 as a **conservative choice pending this document**, and says so where the constant is |
| **MCC 118 electrical specification** [link](https://mccdaq.github.io/daqhats/_static/esmcc118.pdf) | The witness in 4.4. Its **input range** and whether it offers an **external trigger** are the two things 4.4 names as still open against it | **Unread.** Every range it is likely to offer contains 3.3 V, which is a reason to expect it to be fine and **not** a reason to skip reading it |
| **Raspberry Pi 4 datasheet** [link](https://datasheets.raspberrypi.com/rpi4/raspberry-pi-4-datasheet.pdf) | The host the MCC 118 sits on, so it is the other end of the ground wire and of the 3.3 V rule in 4.1 | **Unread.** Nothing here depends on it beyond the supply and logic level |

**Two documents named in this file on purpose and deliberately not linked.**
**UM2407** documents MB1364, the NUCLEO-H743ZI2, and **RM0433** documents the
STM32H743. They appear in 1.2 and in Appendix A as the two documents that are
**not** yours, and a link would invite exactly the mistake those entries exist to
prevent. If you want to confirm the pairing, it is on their own title pages.

### Where the rest of the bench's documents live, and why they are not here

This document is about the board, its silicon and the two wires that have been
run to it. It makes no claim that needs any of the following, so listing them
would pad the appendix and dilute the five above.

| Not listed here | Whose subject it is |
|---|---|
| X-NUCLEO-IKS4A1 and its seven sensors, X-NUCLEO-IKS5A1 and its two, X-NUCLEO-53L8A1 and the VL53L8CX | Chapters 13 to 16, the shields. This file's only shield claim is "one at a time", which needs no datasheet |
| The nRF PPK2 user guide | Chapters 7, 10 and 12, the energy work. The PPK2 is not mentioned in this file at all |
| ADXL345 | Chapter 19, which is the only project here that puts a sensor on a breadboard |
| SIM7020E, SIM7070G and SIM7600E-H, and the SIMCom document sets | Chapter 11, the radio |
| BCM2711 peripherals | Nothing here. The MCC 118 is reached through screw terminals and a driver, not through the host's own registers |
| STEVAL-STWINBX1 | Mentioned once in 4.6, only to say it is a USB-C device rather than a shield or a HAT. That is a disambiguation and not a claim about the part |
| RB-Explorer700, DS3231, the 3.5 inch LCD and ILI9486, the 7 inch DSI display, Raspberry Pi 3B+, NanoPi NEO Air, Allwinner H3, SBC-POW-BB | Other volumes, or on the bench and unused by this board |
| The Renkforce USB to serial cable's bridge chip | **Unknown, and recorded as unknown.** It is commonly a CP2102 and this repository has never read the silicon marking, so no part number is claimed for it |

---

*A closing invitation, because this document asks a lot of you.* If you work
through Part 4's checklist and something does not match your board, or if you read
a manual page cited above and it says something different, that is worth more than
everything in Part 3. The citations and dates are here precisely so that a
disagreement can be located in one step. Please bring it.

*And if you read one of the five unread documents in Appendix C and it closes an
item in 2.9, that is the single most useful thing anybody can do to this file.
Three of the five are ST documents that this bench could not download on
Tuesday 6 October 2026, so a reader with working access to them is better placed
than its author was.*
