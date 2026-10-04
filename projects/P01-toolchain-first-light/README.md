# P01: the toolchain, first light, and printf over the ST-LINK

Status: c=board cpp=none python=none rust=none

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
| The 280 MHz tree | reached Sunday 4 October 2026 by `p01-pll280`: core 280 MHz, AHB and APB1 140 MHz, every one of eight steps reading back what it wrote |
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

    cmake/arm-none-eabi.cmake     the toolchain file, used by every project
    CMakeLists.txt                the firmware build, at the repository root
    c/ld/stm32h7a3zi.ld           the linker script, lengths from this part
    c/board/startup.c             vector table and reset handler, in C
    c/board/system.c              the clock tree, and what it refuses
    c/board/board.c               LEDs, button, board_init
    c/board/uart.c                the console, polled
    c/board/retarget.c            printf, and the cost of that decision
    c/board/board.h               what this project provides to every other
    c/board/stm32h7a3_regs.h      the registers, and what each one rests on
    c/main.c                  first light

The code lives under `c/` and this directory holds the README and the
specification. The board support is in `c/board/` rather than in `c/` because
every project links it: changing it is a decision about all twenty at once.

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
