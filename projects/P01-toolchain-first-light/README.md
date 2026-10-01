# P01: the toolchain, first light, and printf over the ST-LINK

The project every other one here depends on. It owns the cross toolchain file,
the linker script, the vector table, the reset handler, the clock configuration
and the console, so nothing else has to own any of them.

**State: written, never compiled, never flashed.** There is no
`arm-none-eabi-gcc` on win11 aquamarine as of Thursday 1 October 2026, and no
flashing tool and no `ninja` either. Everything here is written so that the day
those arrive there is something to build rather than a blank directory.

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

| Refused | Why | What it needs |
|---|---|---|
| 280 MHz core clock | the PLL fields, the flash access latency and the voltage scaling are all RM0455's and none has been read | RM0455, the clock tree and power chapters |
| Any claim about time | `board_core_hz()` returns 0, so nothing may time anything | the clock above |
| The console, and therefore printf | the virtual COM port pins are Nucleo-144 convention, not read from the board manual | MB1363, and the clock, since the baud divider comes from it |
| Every GPIO and RCC access | the peripheral base addresses are placeholders | RM0455, the memory map chapter |

Each refusal is a value returned, not a comment. `board_clock_status()`,
`board_console_status()` and `board_core_hz()` are how a project finds out, and
`main.c` prints all three and signals them on the LEDs as well, because a board
with no working console that merely sits there tells a reader nothing.

**Why refuse rather than use a plausible value.** The three clock values each get
the part wrong in a different way. Too little flash latency executes garbage once
the clock rises. Wrong voltage scaling works at room temperature and fails warm.
The wrong PLL divider is the worst of the three: the part runs at a plausible
wrong speed, everything appears to work, and every measured interval in every
later project is wrong by a constant factor. A clean square wave at the wrong
rate is indistinguishable from a right one without an external witness, which is
P06's whole subject.

## Bringing it up

`shared/board/stm32h7a3_regs.h` is the one file to edit, and it says so at the
top. Read RM0455's memory map chapter, replace each `BOARD_PLACEHOLDER`, then
define `BOARD_REGS_CONFIRMED` with the manual revision and the full date you read
it. Record that revision here too. The console needs a second step and a second
document: `BOARD_CONSOLE_PINS_CONFIRMED` after the pins are read from MB1363.

**Use RM0455 and not RM0433.** RM0433 documents the STM32H743, which is the
member of this family the internet is about. Its clock tree, its power and
regulator configuration and its memory map all differ. Code copied from material
that says "STM32H7" does not run slowly here; it produces a board that does not
boot.

## Layout

    cmake/arm-none-eabi.cmake     the toolchain file, used by every project
    CMakeLists.txt                the firmware build, at the repository root
    shared/ld/stm32h7a3zi.ld      the linker script, lengths from this part
    shared/board/startup.c        vector table and reset handler, in C
    shared/board/system.c         the clock tree, and what it refuses
    shared/board/board.c          LEDs, button, board_init
    shared/board/uart.c           the console, polled
    shared/board/retarget.c       printf, and the cost of that decision
    shared/board/board.h          what this project provides to every other
    shared/board/stm32h7a3_regs.h the registers, and the placeholders
    projects/P01-.../main.c       first light

The board support lives in `shared/` rather than here because every project
links it. This directory holds only the application.

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

- Never compiled, so no size figures and no map file reconciliation.
- The interrupt vector positions below the sixteen architectural entries are
  this family's published ones and have not been read from RM0455. The table
  carries only what is certain and leaves the rest as the default handler, so an
  unconfirmed position cannot silently misroute.
- `tools/reconcile_size.py`, which the chapter asks for to sum the map file
  against the size output, waits for a build that produces either.
- Nothing touches option bytes, here or anywhere in this volume, until the
  question of whether a bad write can leave the board unrecoverable is settled.
