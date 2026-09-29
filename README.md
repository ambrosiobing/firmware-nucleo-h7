# Twenty Firmware Projects on the NUCLEO-H7A3ZI-Q

Twenty self-contained firmware projects on one board that is already on the
bench, with three sensor shields, one power instrument, one data acquisition hat
and a drawer of cables. Nothing here needs to be bought.

**312 pages, 101 figures, 20 chapters.** Each chapter stands on its own: it
names its prior art and what it takes from it, configures its peripherals,
states a memory and timing budget, and ends in a number. Build any one of them
by itself:

    python build.py --chapter 19

That writes `chapter-19-caches.pdf` and a matching self-contained `.html` with
its five figures inlined.

**Contents**
[The trap](#the-trap-that-runs-through-this-book) ·
[The twenty chapters](#the-twenty-chapters) ·
[Building](#building) ·
[Checks](#checks) ·
[Layout](#repository-layout) ·
[Requirements](#requirements) ·
[Licence](#licence)

## The trap that runs through this book

**This part is not the STM32H7 that the internet is about.** The STM32H7A3 is
documented by a different reference manual from the popular member of its
family, and the clock tree, the power and regulator configuration and the memory
map all differ. Most tutorials, blog posts, video series and example projects
that say "STM32H7" were written against the other one. Code copied from them
does not merely run slowly; it produces a board that does not boot.

That is not a theoretical caution. Every well-engineered project found during
the research encodes the distinction deliberately, and the ones that do not show
exactly what it costs: one language's only board in this family hardcodes a
different member's memory size in its linker script, and its own comments admit
that two memory domains are unmapped and therefore silently skipped by cache
maintenance.

The rule that follows is short, and every chapter keeps it: **no linker script,
clock configuration or memory map is inherited from a sibling part without being
checked against this part's own manual.** A chapter citing material written for
the other part says so in the sentence that cites it, and a fact that has not
been checked is written as a question for the bench rather than as an assertion.

Two more rules shape every chapter. **No generated vendor code is presented as
reviewed source**: the chapters name the peripherals to configure and the
constraints they must satisfy, then show the code written on top. And **every
measurement names its instrument**: there is no oscilloscope and no logic
analyser on this bench, so a chapter that would be easier with one says so and
gives the substitute.

## The twenty chapters

Difficulty is 1 to 5. Effort is in evenings of about four hours. The four parts
are the toolchain and buffering, a low-power node built without its radio, the
sensor shields, and the inside of the Cortex-M7.

| # | Chapter | Theme | Diff. | Effort |
|---|---|---|---|---|
| 1 | [The toolchain, first light, and printf over the ST-LINK](sections/c01.tex) | Cross toolchain, startup, linker script, flashing | 3/5 | 3 evenings |
| 2 | [A single producer, single consumer ring buffer](sections/c02.tex) | Lock-free buffering, memory ordering on the M7 | 3/5 | 2 evenings |
| 3 | [Receiving on interrupt without losing bytes](sections/c03.tex) | Interrupt-driven receive, overrun, two documented library failure modes | 3/5 | 3 evenings |
| 4 | [Circular DMA and the idle line](sections/c04.tex) | Transfer-driven receive, variable length frames, the transfer counter | 4/5 | 3 evenings |
| 5 | [Framing and the hardware CRC unit](sections/c05.tex) | Byte stuffing against a length prefix, CRC-16, the peripheral's reversal settings | 3/5 | 3 evenings |
| 6 | [Sampling on a timer at exactly 1 kHz](sections/c06.tex) | Timer-triggered acquisition, proving the rate externally | 4/5 | 4 evenings |
| 7 | [Stop mode, RTC wake, and a battery number](sections/c07.tex) | Low-power modes, clock restore after wake, charge per cycle | 5/5 | 4 evenings |
| 8 | [The node's state machine: sense, feature, and a transmit that is a stub](sections/c08.tex) | Application state machine, bare metal | 3/5 | 3 evenings |
| 9 | [The payload codec and its Python twin](sections/c09.tex) | Bit packing, CBOR, round-trip property tests | 3/5 | 3 evenings |
| 10 | [Where the energy goes: wake, sense, compute, send](sections/c10.tex) | Marker pins, per-phase charge accounting | 4/5 | 3 evenings |
| 11 | [An AT engine that never blocks](sections/c11.tex) | Asynchronous command queue, unsolicited results, timeouts, backoff | 4/5 | 4 evenings |
| 12 | [Energy as a regression test, and the rig that runs it](sections/c12.tex) | Host unit tests, size gate, hardware in the loop, watchdogs | 4/5 | 4 evenings |
| 13 | [The IKS4A1: FIFO, watermark, interrupt](sections/c13.tex) | Sensor FIFO, watermark interrupt, the three bus topologies | 4/5 | 4 evenings |
| 14 | [The IKS5A1: low and high g at once, wide pressure](sections/c14.tex) | Simultaneous acceleration ranges, dual full-scale barometer | 4/5 | 4 evenings |
| 15 | [Eight by eight time of flight, decided on the MCU](sections/c15.tex) | Sensor firmware upload, zone reduction, hysteresis | 4/5 | 4 evenings |
| 16 | [Where the classifier runs: sensor, MCU or host](sections/c16.tex) | In-sensor engine against on-MCU model against host, measured | 5/5 | 6 evenings |
| 17 | [The same driver twice: vendor layer against registers](sections/c17.tex) | Abstraction layers, code size and cycles, and the Rust variant | 4/5 | 5 evenings |
| 18 | [Transforms and filters with a numerical acceptance test](sections/c18.tex) | CMSIS-DSP, float32 against Q15, cycles, memory placement | 4/5 | 4 evenings |
| 19 | [Caches, the MPU, and why DMA reads stale bytes](sections/c19.tex) | D-cache maintenance, MPU regions, which engine reaches which memory | 5/5 | 4 evenings |
| 20 | [Three jobs at once: the node as an RTOS application](sections/c20.tex) | Static allocation, synchronisation, measured latency distribution | 5/5 | 6 evenings |

Every chapter has the same sections: why it exists, the prior art and what to
reuse, the parts it uses, a system architecture figure, the peripheral
configuration, the wiring, a memory and timing budget, a software design in UML,
a data-flow sketch, a repository layout, numbered steps with real commands and
real code, measurable acceptance criteria, the variants it touches, pitfalls,
best practices, stretch goals, a sourced roadmap, the evidence to publish, and
its sources.

## Building

    python build.py --chapter 19     one chapter, PDF and self-contained HTML
    python build.py --chapters       all twenty, one file each
    python build.py                  the whole book, PDF and one HTML file

Built output is not committed. The book and the individual chapters are
artefacts of this source, they are regenerated in a couple of minutes, and
keeping them out of the history keeps the repository small and every published
file traceable to the commit it came from.

## Checks

    python lint.py                             house rules over every chapter
    python crosscheck.py                       book-level consistency
    python build.py --check sections/c19.tex   compile one chapter and report on it

`lint.py` checks prose for em and en dashes, non-ASCII characters, violent
idioms and the required section skeleton, and checks code blocks for non-ASCII
and for lines longer than the page can print. `crosscheck.py` checks what
per-chapter linting cannot see: the variant matrix, figure coverage,
cross-references, chapter titles against the authoring guide, and that every
date is written in full. Both run on every push, see
[`.github/workflows/checks.yml`](.github/workflows/checks.yml).

## Repository layout

| Path | What it is |
|---|---|
| `sections/cNN.tex` | one file per chapter, 01 to 20 |
| `sections/front.tex` | about, the board, the trap, conventions |
| `sections/appendix.tex` | the scored project catalogue, board reference, instruments, vocabulary, reference library, variant matrix, languages, courses, roadmaps |
| `figures/cNN_{arch,wiring,uml,mem,timing}.tex` | five figures per chapter |
| `main.tex` | preamble, authoring macros, four parts |
| `tikz_preamble.tex` | shared TikZ and circuitikz styles, including the memory map, register field, clock tree, interrupt vector, cache and timing styles |
| `build.py` | figures to SVG, PDF, per-chapter builds, and the HTML converter |
| `lint.py` | house-style check |
| `crosscheck.py` | book-level consistency |
| `AUTHORING.md` | the contract every chapter follows, and the confirm-before-writing list |
| `SOURCE.md` | the prior-art pool, with what each source gives, what it does not, and its licence |
| `build/` | scratch output, ignored, safe to delete |

Chapter files use a `c` prefix so that a cross-reference or a copied figure can
never silently resolve against a sibling volume's files. The book's identity
lives in exactly one `DOC` block per tool file, and both `build.py` and
`lint.py` refuse to run if the folder name stops matching it.

## Requirements

MiKTeX or TeX Live with `pdflatex`, `latex`, `dvisvgm`, `circuitikz`,
`tcolorbox` and `listings`; Python 3.10 or newer. No Python package outside the
standard library is needed.

## Licence

MIT, see [`LICENSE`](LICENSE). The book leans on a great deal of other people's
work: every chapter's Sources section is split into normative references and
reusable implementations, and records the licence of each, because a reader is
told to publish the result as portfolio evidence and needs to know what that
commits them to before writing code around a dependency.
