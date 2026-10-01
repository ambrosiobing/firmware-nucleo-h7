# Twenty Firmware Projects on the NUCLEO-H7A3ZI-Q

Twenty self-contained firmware projects on one board that is already on the
bench, with three sensor shields, one power instrument, one data acquisition hat
and a drawer of cables. Nothing here needs to be bought.

**20 chapters, 101 figures.** Each chapter stands on its own: it names its prior
art and what it takes from it, configures its peripherals, states a memory and
timing budget, and ends in a number. Each has a project beside it that is the
code for the same subject.

**Read a chapter.** Everything here is Markdown with its figures beside it, so it
reads in the browser with nothing to install and nothing to download. Start with
[About this volume](chapters/00-about-this-volume.md), or take a chapter from
[the table below](#the-twenty-chapters) and its project from
[`projects/`](projects/README.md).

One chapter and one project at a time is how this is meant to be read. There is
no assembled volume here to download, by design: no PDF, no single HTML file, no
concatenated edition. The PDF and the HTML are build products that stay on the
authoring machine and are never committed, and `.gitignore` enforces that.

**Contents**
[Read it](chapters/) ·
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
| 1 | [The toolchain, first light, and printf over the ST-LINK](chapters/01-the-toolchain.md) | Cross toolchain, startup, linker script, flashing | 3/5 | 3 evenings |
| 2 | [A single producer, single consumer ring buffer](chapters/02-a-single-producer.md) | Lock-free buffering, memory ordering on the M7 | 3/5 | 2 evenings |
| 3 | [Receiving on interrupt without losing bytes](chapters/03-receiving-on-interrupt-without-losing-bytes.md) | Interrupt-driven receive, overrun, two documented library failure modes | 3/5 | 3 evenings |
| 4 | [Circular DMA and the idle line](chapters/04-circular-dma-and-the-idle-line.md) | Transfer-driven receive, variable length frames, the transfer counter | 4/5 | 3 evenings |
| 5 | [Framing and the hardware CRC unit](chapters/05-framing-and-the-hardware-crc-unit.md) | Byte stuffing against a length prefix, CRC-16, the peripheral's reversal settings | 3/5 | 3 evenings |
| 6 | [Sampling on a timer at exactly 1 kHz](chapters/06-sampling-on-a-timer-at-exactly-1-khz.md) | Timer-triggered acquisition, proving the rate externally | 4/5 | 4 evenings |
| 7 | [Stop mode, RTC wake, and a battery number](chapters/07-stop-mode.md) | Low-power modes, clock restore after wake, charge per cycle | 5/5 | 4 evenings |
| 8 | [The node's state machine: sense, feature, and a transmit that is a stub](chapters/08-the-nodes-state-machine.md) | Application state machine, bare metal | 3/5 | 3 evenings |
| 9 | [The payload codec and its Python twin](chapters/09-the-payload-codec-and-its-python-twin.md) | Bit packing, CBOR, round-trip property tests | 3/5 | 3 evenings |
| 10 | [Where the energy goes: wake, sense, compute, send](chapters/10-where-the-energy-goes.md) | Marker pins, per-phase charge accounting | 4/5 | 3 evenings |
| 11 | [An AT engine that never blocks](chapters/11-an-at-engine-that-never-blocks.md) | Asynchronous command queue, unsolicited results, timeouts, backoff | 4/5 | 4 evenings |
| 12 | [Energy as a regression test, and the rig that runs it](chapters/12-energy-as-a-regression-test.md) | Host unit tests, size gate, hardware in the loop, watchdogs | 4/5 | 4 evenings |
| 13 | [The IKS4A1: FIFO, watermark, interrupt](chapters/13-the-iks4a1.md) | Sensor FIFO, watermark interrupt, the three bus topologies | 4/5 | 4 evenings |
| 14 | [The IKS5A1: low and high g at once, wide pressure](chapters/14-the-iks5a1.md) | Simultaneous acceleration ranges, dual full-scale barometer | 4/5 | 4 evenings |
| 15 | [Eight by eight time of flight, decided on the MCU](chapters/15-eight-by-eight-time-of-flight.md) | Sensor firmware upload, zone reduction, hysteresis | 4/5 | 4 evenings |
| 16 | [Where the classifier runs: sensor, MCU or host](chapters/16-where-the-classifier-runs.md) | In-sensor engine against on-MCU model against host, measured | 5/5 | 6 evenings |
| 17 | [The same driver twice: vendor layer against registers](chapters/17-the-same-driver-twice.md) | Abstraction layers, code size and cycles, and the Rust variant | 4/5 | 5 evenings |
| 18 | [Transforms and filters with a numerical acceptance test](chapters/18-transforms-and-filters-with-a-numerical-acceptance-test.md) | CMSIS-DSP, float32 against Q15, cycles, memory placement | 4/5 | 4 evenings |
| 19 | [Caches, the MPU, and why DMA reads stale bytes](chapters/19-caches.md) | D-cache maintenance, MPU regions, which engine reaches which memory | 5/5 | 4 evenings |
| 20 | [Three jobs at once: the node as an RTOS application](chapters/20-three-jobs-at-once.md) | Static allocation, synchronisation, measured latency distribution | 5/5 | 6 evenings |

Every chapter has the same sections: why it exists, the prior art and what to
reuse, the parts it uses, a system architecture figure, the peripheral
configuration, the wiring, a memory and timing budget, a software design in UML,
a data-flow sketch, a repository layout, numbered steps with real commands and
real code, measurable acceptance criteria, the variants it touches, pitfalls,
best practices, stretch goals, a sourced roadmap, the evidence to publish, and
its sources.

## Building

What is published is the Markdown, one file per chapter, and that is what
`mdbuild.py` writes:

    python mdbuild.py                the Markdown edition, chapters and figures

The PDF and the HTML are for the author's own proofreading on the authoring
machine. They are **never committed and never published**, in whole or per
chapter:

    python build.py --chapter 19     one chapter, PDF and HTML, local only
    python build.py                  the whole volume, PDF and HTML, local only

Three reasons, and the first is the binding one. A single assembled volume is not
what this repository offers: a reader takes one chapter and its project, which is
how the material is written and how it is meant to be used. A PDF also renders
nowhere in a repository view, so it would be a download rather than a page. And
both are artefacts of this source, regenerated in a couple of minutes, so keeping
them out of the history keeps every published file traceable to the commit it
came from.

`.gitignore` enforces it rather than leaving it to discipline: `*.pdf`, `*.html`,
the named whole-volume files and the per-chapter ones are all refused.

The figures are the one deliberate exception to not committing built output. They
are committed as SVG, because the Markdown draws no diagram in a browser without
them.

## Checks

    python lint.py                             house rules over every chapter
    python crosscheck.py                       book-level consistency
    python tools/check_links.py                every relative link and anchor
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
| `chapters/NN-title.md` | the Markdown edition, one file per chapter, generated from `sections/` |
| `figures/NAME.svg` | every figure rendered, committed so the Markdown draws in a browser |
| `sections/cNN.tex` | one file per chapter, 01 to 20 |
| `sections/front.tex` | about, the board, the trap, conventions |
| `sections/appendix.tex` | the scored project catalogue, board reference, instruments, vocabulary, reference library, variant matrix, languages, courses, roadmaps |
| `figures/cNN_{arch,wiring,uml,mem,timing}.tex` | five figures per chapter |
| `main.tex` | preamble, authoring macros, four parts |
| `tikz_preamble.tex` | shared TikZ and circuitikz styles, including the memory map, register field, clock tree, interrupt vector, cache and timing styles |
| `mdbuild.py` | the Markdown converter, which reuses `build.py`'s parser |
| `build.py` | figures to SVG, PDF, per-chapter builds, and the HTML converter |
| `lint.py` | house-style check |
| `crosscheck.py` | book-level consistency |
| `AUTHORING.md` | the contract every chapter follows, and the confirm-before-writing list |
| `SOURCE.md` | the prior-art pool, with what each source gives, what it does not, and its licence |
| `CONTENTS.md` | the chapter table, generated, which the table above follows |
| `build/` | LaTeX scratch output, ignored, safe to delete |

The code lives in the same repository as the book, and these are its paths.
Chapter NN is the written design for project PNN.

| Path | What it is |
|---|---|
| [`projects/`](projects/README.md) | twenty project directories, `P01` to `P20`, each with its own README and state |
| `shared/` | C that more than one project compiles: the ring buffer and its four ordering modes, the payload codec, the on-chip instruments |
| `firmkit/` | Python that more than one project imports: the codec twin, the rate analysis, the serialisation-size arithmetic |
| `tests/` | every suite, central, and none of which touches a device |
| `tools/` | the codec generator, the host build, and the checks that prove the checks work |
| `requirements.txt` | `pytest`, and nothing else |
| `requirements-hardware.txt` | what only matters when something is plugged in; the suite and the runner never install it |
| `build-host/` | code build output, ignored, deliberately not `build/` so the two cannot delete each other's work |

Three projects are written: P02, P06 and P09. The rest carry a README naming what
they are and that they have not started. The whole suite runs on a laptop with no
board, no probe and no Raspberry Pi, which is why it is the half that could be
finished first.

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
