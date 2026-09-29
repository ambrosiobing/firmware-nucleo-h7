# Authoring guide for the NUCLEO-H7A3ZI-Q volume

Read `sections/c01.tex` and `figures/c01_*.tex` first: they are the reference for
tone, depth, structure and figure style. Every chapter must compile alone with

    python build.py --check sections/cNN.tex

and finish with `== RESULT: CLEAN` (no pdflatex errors, all five figures render
to SVG, no unknown macro, no non-ASCII character in a code block, no code line
over its limit). Then

    python lint.py sections/cNN.tex

must print `clean`. It enforces the house rules the compiler cannot see: no em
or en dashes and no `--` in prose, ASCII-only code blocks, code line length, no
violent idioms, no mention of tooling, the full subsection skeleton, and all
five figures present.

A label must never sit on top of a symbol or another label. Circuit labels go
beside the branch, not centred on the component; sequence-diagram notes go
outside the lifelines; memory-map addresses go outside the column. Render the
figure and look at it before moving on. The linter cannot see this and three
overlapping figures reached the sibling volume before anyone did.

Before a release, prove the toolchain has not drifted from its sibling:

    python build.py --drift ../EmbeddedLinux_Top20/build.py

Everything outside the `DOC` block at the top of each file should match. It will
not report clean until the sibling is parameterised the same way, which is a
separate small task; until then, read the diff and confirm every hunk is one of
this volume's deliberate changes.

---

## THE TRAP THAT RUNS THROUGH THIS BOOK

**This part is not the STM32H7 that the internet is about.**

The STM32H7A3 is documented by **RM0455**. The popular member of the family, the
STM32H743, is documented by **RM0433**. The clock tree, the power and regulator
configuration and the memory map all differ. Most tutorials, blog posts, video
series and example projects that say "STM32H7" were written against the other
one. Code copied from them does not run slowly; it produces a board that does
not boot.

Every well-engineered project encodes this distinction deliberately. The Rust
hardware crate routes `stm32h7a3` through the `stm32h7b3` peripheral module and
selects `rm0455` so the pin mapping is right. MicroPython ships its own
`stm32h7a3.ld` while borrowing the H7B3 alternate-function table. By contrast,
TinyGo's only H7 linker script hardcodes `RAM ORIGIN = 0x24000000, LENGTH = 512K`
from the H753, and its own comments admit two memory domains are unmapped and so
silently skipped by cache maintenance.

**Rule: no linker script, clock configuration or memory map is inherited from a
sibling part without being checked against RM0455.** Any chapter that cites
material written for the H743 says so in the sentence that cites it.

---

## The twenty chapters

Numbering is fixed. Titles may be polished but not renamed in spirit.

| NN | Title | Target board or shield | Theme |
|----|-------|------------------------|-------|
| 01 | The toolchain, first light, and printf over the ST-LINK | Nucleo alone | Cross toolchain, CMake, startup, linker script, flashing |
| 02 | A single producer, single consumer ring buffer | Nucleo alone | Lock-free buffering, memory ordering on the M7 |
| 03 | Receiving on interrupt without losing bytes | Nucleo alone | Interrupt-driven UART, overrun, the documented HAL failure modes |
| 04 | Circular DMA and the idle line | Nucleo alone | DMA receive, variable-length frames, the DMA counter |
| 05 | Framing and the hardware CRC unit | Nucleo alone | COBS and length prefix, CRC-16, the CRC peripheral's reversal settings |
| 06 | Sampling on a timer at exactly 1 kHz | Nucleo + MCC 118 on a Pi | Timer-triggered acquisition, proving the rate externally |
| 07 | Stop mode, RTC wake, and a battery number | Nucleo + PPK2 | Low-power modes, clock restore after wake, charge per cycle |
| 08 | The node's state machine: sense, feature, and a transmit that is a stub | Nucleo alone | Application state machine, bare metal |
| 09 | The payload codec and its Python twin | Nucleo + host | Bit packing, CBOR, round-trip property tests |
| 10 | Where the energy goes: wake, sense, compute, send | Nucleo + PPK2 | Marker pins, per-phase charge accounting |
| 11 | An AT engine that never blocks | Nucleo + SIM7020E | Asynchronous command queue, URCs, timeouts, backoff |
| 12 | Energy as a regression test, and the rig that runs it | Nucleo + Pi + PPK2 | Host unit tests, size gate, hardware in the loop |
| 13 | The IKS4A1: FIFO, watermark, interrupt | Nucleo + X-NUCLEO-IKS4A1 | Sensor FIFO, watermark interrupt, the three bus topologies |
| 14 | The IKS5A1: low and high g at once, wide pressure | Nucleo + X-NUCLEO-IKS5A1 | Simultaneous ranges, dual full-scale barometer |
| 15 | Eight by eight time of flight, decided on the MCU | Nucleo + X-NUCLEO-53L8A1 | Sensor firmware upload, zone reduction, hysteresis |
| 16 | Where the classifier runs: sensor, MCU or host | Nucleo + IKS4A1 + Pi | In-sensor engine against on-MCU model against host, measured |
| 17 | The same driver twice: vendor layer against registers | Nucleo + a shield | HAL against LL, code size and cycles, and the Rust variant |
| 18 | Transforms and filters with a numerical acceptance test | Nucleo alone | CMSIS-DSP, float32 against Q15, cycles, memory placement |
| 19 | Caches, the MPU, and why DMA reads stale bytes | Nucleo + ADXL345 | D-cache maintenance, MPU regions, which engine reaches which memory |
| 20 | Three jobs at once: the node as an RTOS application | Nucleo + IKS4A1 | FreeRTOS task set, static allocation, measured latency distribution |

Cross-reference other chapters as `Chapter 5` in prose; `\ref{sec:c05}` works.

### Non-duplication

- `../EmbeddedLinux_Top20` Project 12 owns the Linux side of the same cable: the
  D-Bus service on the Raspberry Pi. This volume owns the firmware side and does
  not repeat the D-Bus material.
- `../EmbeddedLinux_KitLabs20` labs P06, P07, P08 and P18 put this Nucleo on the
  bench but always flash a vendor sample and teach the Linux host. Their text is
  not repeated. Their I2C address map and the one-shield-at-a-time rule are
  authoritative and are reused.
- The STWIN.box appears in **no chapter**. It is the committed subject of the
  EdgeAI condition-monitoring build, and keeping it out is deliberate.
- `daqring` is a finished Linux kernel-module portfolio piece. Nothing here
  repeats it.

---

## Variants: the book-level matrix

Joseph asked for the same exercise solved several ways. Twenty chapters times
seven axes is not a book. So: **each variant is built in full exactly once**, in
the chapter where it teaches most, and **referenced** everywhere else it applies.
Appendix F prints this matrix.

### The seven axes

| Axis | Variants, simplest first |
|---|---|
| Execution model | Polled loop, interrupt with a flag, interrupt into a buffer, DMA, DMA double buffered, an RTOS task |
| Synchronisation | A volatile flag, binary or counting semaphore, mutex with priority inheritance, queue, direct task notification, event group, stream buffer |
| Time and safety | SysTick, a general timer, a low-power timer, the RTC, tickless idle; IWDG against WWDG |
| Peripheral substitution | The same job on a different peripheral |
| Operating system | Bare metal, FreeRTOS, Zephyr |
| Language | C baseline, then C++, Rust and MicroPython on the target; Python, Node.js or Java on the host |
| Intelligence and reach | Local only, edge host, classifier in the sensor, model on the MCU |

### Where each variant is built in full

Claim a variant in your chapter only if this table says it is yours.

| Variant | Built in full in |
|---|---|
| Polled loop, interrupt with a flag | Chapter 3 |
| Interrupt into a ring buffer | Chapter 2 and 3 |
| DMA, circular, idle line | Chapter 4 |
| DMA double buffered | Chapter 6 |
| RTOS task set, static allocation | Chapter 20 |
| Semaphore, mutex, queue, task notification | Chapter 20 |
| Event group, stream buffer | Chapter 20, referenced from 12 |
| SysTick, general timer | Chapter 6 |
| Low-power timer, RTC, tickless idle | Chapter 7 |
| IWDG and WWDG | Chapter 12 |
| Peripheral substitution | Chapter 17 |
| Zephyr | Chapter 20, referenced from 13 |
| C++ | Chapter 9 |
| Rust | Chapter 17 |
| MicroPython | Chapter 1, as the five-minute path, and Chapter 9 |
| Host in Python | Chapter 3 |
| Host in Node.js or Java | Chapter 12 |
| Edge host over a framed link | Chapter 9 and 11 |
| Classifier in the sensor, on the MCU, on the host | Chapter 16 |

The vendor's own example tree for this board already carries one problem solved
polled, on interrupt and by DMA, in three sibling directories, plus parallel
register-level and mixed-level trees. That is the precedent for this format and
those basic examples are permissively licensed. Read `SOURCE.md` before writing.

---

## Inventory (only these parts; nothing is bought)

**The board.** NUCLEO-H7A3ZI-Q: STM32H7A3ZIT6Q, Cortex-M7 at 280 MHz, 2 MB
flash, about 1.4 MB SRAM, SMPS (the `-Q` suffix), on-board ST-LINK V3E providing
a debug probe, a virtual COM port and a drag-and-drop disk, Zio and ST morpho
connectors. **No Ethernet MAC. No on-board SD card socket. No external flash.
No CAN transceiver.** Confirm each against the datasheet before writing.

**Shields, one at a time on the Arduino header.** X-NUCLEO-IKS4A1, X-NUCLEO-IKS5A1,
X-NUCLEO-53L8A1. Never two at once: address collisions and the 3V3 budget.

**Other MCU boards.** STEVAL-STWINBX1 (excluded from all chapters by design),
Joy-it SBC-NodeMCU-ESP32, Joy-it SBC-ESP8266-PROG.

**Linux hosts.** Raspberry Pi 3, 3B+, 4; NanoPi NEO Air.

**Sensors, HATs and instruments.** DFRobot SEN0032 (ADXL345), MCC 118 DAQ HAT
(8 channels, 12-bit, 100 kS/s aggregate), nRF-PPK2 (ammeter or source meter, up
to 1 A, eight digital inputs), Waveshare SIM7600E-H, SIM7070G and SIM7020E,
JOY-iT RB-Explorer700, Waveshare 3.5 inch RPi LCD (A), official 7 inch DSI
touchscreen, official Raspberry Pi keyboard.

**Cables and passives.** Renkforce USB/TTL cable, Joy-it SBC-POW-BB, breadboard,
jumper wires, green/yellow/red LEDs, resistors 220 Ohm to 1 kOhm, microSD cards,
USB supplies.

**Host.** Windows 11 with WSL2, and the Linux boards themselves.

### Instruments, and what the bench does not have

**There is no oscilloscope, no logic analyser and no Joulescope.** Confirmed by
Joseph on Sunday 20 September 2026. Every timing and energy claim comes from:

- the **nRF-PPK2**, current plus its eight digital inputs time-aligned with the
  current trace, which is this book's logic recorder for slow signals;
- the **MCC 118 on a Raspberry Pi** at 100 kS/s aggregate, as the calibrated
  analogue reference;
- the board's own **timers and the DWT cycle counter**.

A chapter that would be easier with a scope says so in one sentence and gives
the substitute. There is also **no soldering iron**, so nothing that needs a
solder bridge moved is proposed.

---

## Chapter structure

All headings in this order. Use `\subsection*{...}`.

```
\project{NN}{Title}{Target board}{Theme}
\begin{keyfacts} \item[Board] \item[Peripherals] \item[Toolchain] \item[Operating system]
  \item[Difficulty] n of 5 \item[Effort] ... \item[Deliverable] ... \end{keyfacts}
\subsection*{Why this project}                 2 to 3 paragraphs: what it teaches, why it is unique
\subsection*{Prior art and what to reuse}      table: Source | What it gives | What it does not | Licence
                                               then a paragraph naming what is left to write
\subsection*{Parts from the inventory}         table: Part | Role | Interface
\subsection*{System architecture}              \diagram{cNN_arch}{...}
\subsection*{Peripheral configuration}         table: Peripheral | Mode | Clock | Pins and function | IRQ and DMA
\subsection*{Wiring}                           \diagram{cNN_wiring}{...}  schematic, or bench art where nothing is wired
\subsection*{Memory and timing budget}         \diagram{cNN_mem}{...} + table: Quantity | Budget | Measured | Margin
\subsection*{Firmware design (UML)}            \diagram{cNN_uml}{...} + paragraph
\subsection*{Data flow (ASCII)}                \begin{asciiart} ... \end{asciiart}, max 112 columns
\subsection*{Repository layout}                \begin{plaincode} tree \end{plaincode}
\subsection*{Steps}                            \begin{steps} 7 to 10 steps with real commands and code
\subsection*{Build, flash and debug}           \diagram{cNN_timing}{...} + commands + how to recover a dead board
\subsection*{Verification and acceptance criteria}   measurable bullets
\subsection*{Variants}                         table: Axis | Variant | What changes | Cost | Built in full in
\subsection*{Pitfalls}
\subsection*{Best practices applied}
\subsection*{Stretch goals}
\subsection*{Roadmap and next steps}           sourced from SOURCE.md, never invented
\subsection*{Portfolio evidence}
\subsection*{Sources}                          two lists: Normative references, Reusable implementations
```

Target length: **14 to 16 PDF pages**, 500 to 800 lines of LaTeX. That is 10 to
12 pages of chapter plus 2 to 4 of variants plus a page of roadmap.

### The five figures

| Name | Contents | Styles |
|---|---|---|
| `cNN_arch` | System block diagram: silicon, firmware layers, host, debugger | `hw` `fw` `kern` `user` `ext` `layer` `flow` `bus` `irq` |
| `cNN_wiring` | circuitikz schematic where there is wiring, bench art where there is not | `board` `hat` `pinrow` `cable` `usbcable` `ledsym`, or circuitikz |
| `cNN_uml` | Sequence, state machine, class or component diagram | `actor` `lifeline` `msg` `reply` `activation` `state` `trans` `umlnote` `rtstate` |
| `cNN_mem` | The silicon figure: memory map, register bit field, or clock tree | `mmap*` `reg*` `osc` `pll` `clkmux` `clkdiv` `clkdom` |
| `cNN_timing` | The time figure: signal timing, NVIC vectors and priority, DMA and cache coherency, or an RTOS schedule | `signal` `\clocktrain` `\busval` `vec*` `prio` `master` `cache` `incoherent` `task*` `\slice` |

The last two are each a choice of four. Assigned so nothing is drawn twice:
clock tree in `c01`, memory map in `c19`, register bit fields in `c05` and `c17`,
NVIC in `c03`, cache and DMA in `c19`, signal timing in `c04` and `c06`, RTOS
schedule in `c20`.

---

## LaTeX subset

- Headings: `\subsection*{}`, `\subsubsection*{}`, `\paragraph{}`.
- Inline: `\textbf{}`, `\emph{}`, `\texttt{}`, `\url{}`, `\href{}{}`, `\verb|...|`,
  `\ref{sec:cNN}`, `\newline` (only before a very long URL in Sources), `\ldots`,
  `\textmu{}`, `\textdegree{}`, `\textohm{}`, simple `$...$`.
- Lists: `itemize`, `enumerate`, `description` (`\item[Label]`), `steps`.
- Boxes: `keyfacts` (top only), `\begin{note}{Title} ... \end{note}`.
  **A comma in a note title breaks the box.** Titles stay comma free.
- Code: `asciiart` `ccode` `cppcode` `rustcode` `shellcode` `pycode` `makecode`
  `dtscode` `yamlcode` `asmcode` `ldcode` `plaincode`. Pure ASCII.
- Tables: `\begin{table}[H]\centering\small ... \toprule \midrule \bottomrule ...
  \caption{...} \end{table}`. `p{..mm}` columns, total at most 165 mm. Escape
  `_ & % #`. No code environments inside tables.
- Figures only through `\diagram{name}{Caption}`, with `figures/name.tex` holding
  exactly one `tikzpicture` or `circuitikz`.
- **Forbidden**: `\section`, `\chapter`, `\footnote`, `\includegraphics`,
  `minipage`, custom packages, `\input`, `\cite`, unicode arrows, and en or em
  dashes. Use a colon, a comma or "to" instead of a dash. Hyphens in compound
  words are fine. `\newcommand` is forbidden in `sections/`; it is allowed inside
  `figures/`, which the HTML converter never parses.

### Line lengths

**96 columns for code, 112 for ASCII art.** Both are enforced by the linter and
warned by the builder, and both come from the geometry.

Text width is 168 mm, which is 478 pt. A code column at `\footnotesize` with
`basewidth=0.48em` is 4.32 pt. The binding case is not a top-level listing but a
listing nested inside `steps`, where almost every code block in this book lives:
the step label and indent cost about 45 pt and the frame about 13 pt, leaving
97 columns. So 96, with one column of slack.

ASCII art is unframed, one size smaller and never nested: 122 columns fit in
print. But the HTML edition wraps at about 114, so 112 clears both.

Note that `breaklines=true` means an over-long code line **wraps silently**. It
produces no overfull warning. The linter is the only thing that catches it.

---

## Figure conventions

Styles available, from `tikz_preamble.tex`:

- **Blocks**: `blk` `hw` (silicon) `kern` (RTOS) `user` (application) `ext`
  (host) `fw` (firmware module) `wide` `narrow` `layer` `layerlabel`;
  arrows `flow` `bus` `irq`; edge labels `lbl`.
- **UML**: `umlclass` `actor` `lifeline` `msg` `reply` `activation` `state`
  `initial` `final` `trans` `umlnote` `rtstate`.
- **Bench art**: `board` `hat` `breadboard` `pinrow` `cable` `usbcable` `ledsym`.
- **Memory map**: `mmflash` `mmram` `mmperiph` `mmext` `mmres` `mmaddr` `mmgap`
  `mpu`; macro `\memregion{y}{height}{style}{name}{address}` and `\memgap{y}`.
  Lengths are plain numbers in millimetres. Draw low addresses at the bottom.
- **Register fields**: `regfield` `regrsvd` `regro` `regset` `regbit` `regname`
  `regreset`; macros `\bitscale` and `\bitfield{hi}{lo}{style}{label}`, with
  `\renewcommand{\regwidth}{16}` for a 16-bit register. Bit `\regwidth-1` is
  leftmost, as in every reference manual. A 32-bit register at 4 mm per cell is
  128 mm wide and fits.
- **Clock tree**: `osc` `pll` `clkmux` `clkdiv` `clkgate` `clkdom` `clkline`
  `clkfreq`.
- **NVIC**: `vecsys` `vecirq` `vecfree` `vecnum` `prio` `prioaxis` `preempt`
  `tailchain`; macro `\vecentry{row}{style}{number}{handler}`. Split a long
  vector table into system exceptions and the few interrupts the chapter uses.
- **DMA and cache**: `master` `busmatrix` `memblk` `cache` `cwclean` `cwdirty`
  `cwstale` `dmaflow` `cpuflow` `incoherent`; macro `\cacheline{x}{y}{style}{label}`.
- **Timing**: `signal` `signalb` `signame` `tick` `tspan` `tlabel` `sample`
  `hiz`; macros `\clocktrain{y}{x}{cycles}{period}` and `\busval{y}{x}{w}{text}`.
- **RTOS schedule**: `taskrun` `taskready` `taskblock` `taskisr` `tasklane`
  `taxis` `release` `deadline`; macros `\slice{lane}{t}{dur}{style}{label}`,
  `\lane{y}{name}` and `\deadlinemark{t}{height}`.

Rules:

- Every figure at most **16 cm wide and 12 cm high**.
- **Solid fills only.** Figures are built latex to dvi to dvisvgm, a path on
  which PDF transparency is silently dropped. No `opacity`, no `fadings`.
- Any computed coordinate must be braced: `at ({(\regwidth-1-#1)*\regunit mm},0mm)`.
  Without the braces TikZ reads `(16-0` as a node name.
- `\\` inside a node only with `align=center`. Escape `_` and `&` in node text.
- No new package without checking it is DVI-safe, and it must be added to
  `tikz_preamble.tex`, which both `main.tex` and the per-figure wrapper input.
  A package added to `main.tex` alone builds the PDF and silently breaks every
  figure in the HTML.
- Pin numbers must be real. Arduino D14 and D15 reach PB9 and PB8 on the
  Nucleo-144 convention; the virtual COM port is usually USART3 on PD8 and PD9.
  **Both are flagged in the confirm list below and neither is written as fact
  until checked in the board manual.**

---

## Writing rules

- Accurate and specific: real package names, real register names, real commands.
  When support is uncertain, say so and give the fallback. Never invent a
  register map, a crate feature or an API name.
- **Reuse before writing.** Every chapter opens its technical work by naming the
  prior art and saying what it takes from each source and what is left to write.
  A chapter that reimplements a maintained library without saying why has failed.
- **Every measurement names its instrument**, or says it has not been taken.
  Never invent a microamp figure; write "not measured".
- **Every dependency names its licence** before the reader writes code around it.
  See the licence categories in `SOURCE.md`.
- Professional register, no hype, no jokes. No violent idioms: say "terminate
  the process", "stress the system", "the cause is".
- No em or en dashes, and no `--` in prose.
- **Dates are written in full and redundantly: weekday, day, month, year**, in
  prose and in every table row. Never a bare weekday, never a month without a
  year.
- No tooling attribution anywhere, in prose, in comments or in commit messages.
  The scan that enforces this lives outside the repository on purpose, because
  a rule that spells the words it forbids would put them into the very thing it
  protects. Run it before anything is published.
- Each chapter stays unique. Reference another chapter, do not repeat it.
- Safety notes where relevant: 3.3 V logic only, never 5 V into a GPIO, the PPK2
  measures at most 1 A, the cellular modems need 2 A peaks from their own supply
  and cannot be fed from the board, disconnect the red 5 V lead of the USB/TTL
  cable.

---

## Confirm before writing

Nothing on this list is written as fact until checked against **RM0455**, the
**MB1363** board user manual, the STM32H7A3xI datasheet, or the relevant shield
user manual. Anything still open is written as a question for the bench, not as
an assertion. Tick items off here as they are settled.

### Settled on Sunday 20 September 2026

Two of the three blocking items are closed, from two independent sources that
name this exact board: the upstream board description in the other operating
system's tree, and the MicroPython board definition in its main branch. Both are
machine-readable and were read directly, not from a summary.

1. **Ethernet: there is none.** The upstream peripheral list for this board
   names no Ethernet, no MAC and no network interface anywhere. The bench
   document that claimed "Ethernet and USB" is wrong, and so was one of the two
   research reads of this same file. **Every tutorial pairing this family with a
   network stack over Ethernet is for a different part**, and that goes in the
   front-matter warning. Any network path here goes through a coprocessor or a
   modem.
2. **The 32.768 kHz crystal is fitted.** The upstream list states "32.768 kHz
   crystal oscillator" and the MicroPython board sets `MICROPY_HW_RTC_USE_LSE`.
   This was the highest-risk item in the plan: chapters 7, 10 and 20 can use the
   real-time clock and claim its accuracy.
3. **The high-speed clock comes from the debugger, in bypass mode, at 8 MHz.**
   MicroPython sets `MICROPY_HW_CLK_USE_BYPASS`, then multiplier 140 over
   dividers 2 and 2, which is 8 / 2 * 140 / 2 = 280 MHz. That is a verified,
   working configuration for chapter 1 to start from and to explain.
4. **LEDs and button, both sources agreeing exactly**: LD1 green on PB0, LD2
   yellow on PE1, LD3 red on PB14, user button on PC13. Safe to write as fact.
5. **The virtual COM port is USART3.** MicroPython drives its console on UART 3
   at 115200. The *pins* PD8 and PD9 are still the Nucleo-144 convention rather
   than a checked fact; confirm them in the board manual before printing them.
6. **Peripheral inventory from the upstream list**, useful for the chapter 6
   timer inventory and the chapter 18 analogue work: two 32-bit timers and
   fifteen 16-bit, six SPI, four I2C, five USART and five UART, two CAN FD, two
   16-bit converters with 24 channels at 3.6 MSPS, a 12-bit output converter, a
   random number generator, sixteen transfer channels, a graphics accelerator, a
   still-image codec and a display controller. **No cryptographic accelerator
   appears**, which matches expectation.

### Still blocking, checked before the chapter that needs it

7. **Can a bad option-byte write leave the board unrecoverable without external
   tooling?** If there is any such risk, no chapter touches option bytes. Write
   the safety note before the steps, not after.

### Board, MB1363

8. The virtual COM port pinout. USART3 on PD8 and PD9 is the usual convention and
   the existing workbook flags it as unverified. Every chapter depends on it.
5. The maximum reliable baud rate on that port. The sibling volume asserts
   921600; chapter 3's measurement depends on where loss begins.
6. Which jumper carries the IDD current measurement, exactly what it isolates
   (core only, or core plus shield), whether ST-LINK current is excluded, and
   whether the PPK2 can be wired across it directly.
7. Whether SWO on PB3 is routed to the ST-LINK V3E and whether that probe does
   trace at all. If not, the UART-backed event ring is the only option and
   chapters 3 and 20 must say so.
8. Whether the board has a user USB connector, of what type, and whether the USB
   peripheral is usable at the same time as the debugger.
9. Whether HSE is supplied from the ST-LINK by default, at what frequency, and
   the shipped solder-bridge state.
10. Whether the backup domain is usable without a coin cell, and therefore
    whether backup SRAM can hold a crash record across a power cycle.
11. User LED count, colours and pins; the user button pin; and whether any
    collide with a fitted shield.
12. Arduino header to MCU pin mapping: D14 and D15 as I2C1, which SPI instance
    is on D11 to D13, which USART on D0 and D1, which analogue pins reach which
    converter channels.
13. Which Zio pins with timer channels and spare GPIO remain free while a shield
    is fitted, for the PPK2 marker pins.
14. Whether the drag-and-drop disk appears, and whether using it conflicts with
    anything in chapter 1.

### STM32H7A3 silicon, RM0455

15. **SETTLED on Sunday 20 September 2026.** Two independent machine-readable
    sources for this exact part agree to the kilobyte: the MicroPython linker
    script and the upstream device tree in the other operating system. Write
    these as fact, and check only the tightly coupled instruction memory, which
    neither source declares.

    | Region | Base | Size | Notes |
    |---|---|---|---|
    | Flash | `0x0800 0000` | 2048 kB | Dual bank; the split point is a design choice, not a fixed boundary |
    | DTCM | `0x2000 0000` | 128 kB | Not reachable by the main transfer engines. This is the trap of chapter 19 |
    | AXI SRAM | `0x2400 0000` | 1024 kB | Three banks: 256 kB, then 384 kB at `0x2404 0000`, then 384 kB at `0x240A 0000` |
    | Second domain SRAM | `0x3000 0000` | 128 kB | Two banks of 64 kB, the second at `0x3001 0000` |
    | Low-power domain SRAM | `0x3800 0000` | 32 kB | Survives while the other domains sleep, which is why the third transfer engine exists |

    That is 1312 kB of declared SRAM. The commonly repeated "about 1.4 MB"
    figure is only reached by adding the tightly coupled instruction memory,
    which is why it should be stated as 1312 kB plus that memory rather than
    rounded. The transfer-request multiplexers sit at `0x4002 0800` and
    `0x5802 5800`, both with 107 requests.
16. Which transfer engine reaches which memory. Expected: the main engines cannot
    reach the tightly coupled memories, the master engine reaches everything, and
    the third engine reaches only one small memory but survives deep sleep.
    This is chapter 19 and the most common bug in the family.
17. Flash size, whether it is genuinely dual bank, write granularity, the
    write-once error-correction rule, and whether one bank can be read while the
    other is written.
18. Whether the part has a hardware CRC unit and how configurable it is
    (polynomial, initial value, input and output reversal). Chapter 5 depends on
    matching a software reference exactly.
19. Whether the part has a random number generator and whether it has a
    cryptographic accelerator.
20. Whether the part has a high-resolution timer. The chapter 6 timer inventory
    must be correct; the full timer list, which are 32-bit, and which support
    encoder and input capture.
21. Analogue converter count, maximum resolution, maximum rate, the internal
    reference and temperature channels, and the factory calibration word.
22. Whether the digital-to-analogue converter exists, how many channels, and
    whether its output reaches an accessible header pin.
23. The exact combination of voltage scale, supply configuration and flash
    latency required for 280 MHz, and whether the SMPS variant is constrained
    further.
24. Whether the floating-point unit is double precision, and the L1 cache sizes.
25. Whether the cycle counter is usable with no debugger attached, and whether
    any erratum affects it. If it needs a debugger, chapter 6 loses its cheapest
    instrument.
26. Cache line size, and any Cortex-M7 errata on store buffering or cache
    maintenance for this revision.
27. Which low-power modes exist, what each retains, wake sources and wake latency.
28. Independent and window watchdog presence, the debug freeze bits, whether the
    independent watchdog survives Stop mode, and **whether the debug clock must
    be enabled before the freeze register write takes effect**, which is a
    documented silent failure.

### Shields

29. The IKS4A1 sensor complement and I2C addresses, and the jumper settings for
    its three bus topologies (all sensors on one bus, or either IMU as hub).
    Reference: UM3239.
30. The IKS5A1 complement. **Settled from the vendor's product page and three
    corroborating sources**: ISM6HG256X, ISM330IS with ISPU, IIS2DULPX, IIS2MDC,
    ILPS22QS. The LSM6DSV32X and LSM6DSO16IS claims in local notes are wrong.
    Confirm on the silkscreen in a paragraph, then move on. Reference: UM3550.
31. Whether the 53L8A1's bus selection is a jumper and not a solder bridge, and
    which SPI signals it uses. If it needs soldering, chapter 15 is I2C only.
    Reference: UM3120.
32. The time-of-flight sensor's firmware image size and upload time, and how much
    flash it costs. Expected: about 84 kB, on the order of seconds, every
    power-on, because the sensor has no non-volatile memory.

### Instruments and tooling

33. PPK2 digital input count, logic threshold, sample rate, how they are
    time-aligned with the current trace, and whether the export carries them.
34. Whether the PPK2 in source-meter mode can power the board through the IDD
    jumper at the required voltage and current.
35. MCC 118 single-channel against aggregate rate, input range, and whether an
    external trigger exists for synchronisation.
36. Whether male-to-male jumper wires exist in the drawer. Every header on the
    bench is female and nothing can be soldered.
37. Whether the flashing tool's built-in target list covers this part. **It
    does**: the raw target manifest carries entries for STM32H7A3ZITxQ, the exact
    die. An earlier check that read a summary of that file reported none.
    **Lesson: when a claim rests on a large generated file, search the file, not
    a summary of it.**
38. Whether the open debug server supports this part, which configuration files
    to name, and the known problem that trace output does not work over the
    default transport and that the relevant registers moved in this family.

### Language support

39. C++ standard version and what must be disabled. Exceptions cost roughly 10 to
    30 kB of unwinder tables, which on 2 MB of flash is a choice and not a
    necessity; say so rather than repeating the usual claim.
40. Rust: the hardware crate's `stm32h7a3` feature routes to the `stm32h7b3`
    peripheral module and selects `rm0455`. The async framework names
    `stm32h7a3zi` exactly. **There is no worked example for this part in that
    framework**, only for its twin; that gap is worth filling.
41. MicroPython: the board definition exists in the main branch with its own
    linker script, and prebuilt firmware is published. Its declared feature list
    is empty, so do not promise USB host or networking without building it.

---

## Sources and licences

See `SOURCE.md` for the full prior-art pool, the licence categories, and the
roadmap material each chapter's last section draws on. Read it before writing a
chapter. Two rules from it apply everywhere:

- **Never present generated vendor code as reviewed source.** Name the
  peripherals to configure and the constraints they must satisfy, then show the
  code you wrote on top. Never reproduce more than about forty lines of
  generated output.
- **Several of the best free courses carry licences that forbid reproduction in
  a commercial work.** Recommend them; never quote them at length.
