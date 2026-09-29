# Provenance and the prior-art pool

Researched Sunday 20 September 2026. Every address below was checked unless
marked otherwise. Anything that could not be confirmed to exist was dropped
rather than guessed at, and the gaps are named at the end because a gap is a
finding too.

**Re-check every link before the volume is delivered.** Three domains that
matter here block automated checking: the silicon vendor's own site, its wiki,
and the core designer's documentation site. Those are marked
**[browser-check]** and must be opened by hand. The vendor reshuffles its paths,
and the core designer has moved its documentation to a new host.

---

## 1. Where the chapters come from

| Chapter | Source | Original |
|---|---|---|
| 1 to 7 | The seven exercises of the Nucleo H7A3 workbook, written for an interview on Wednesday 2 September 2026 | `../../03_Job_Fit_Assessments/Fraunhofer_IIS_mioty/uebungsbuch_nucleo_h7a3.md` |
| 8 to 20 | The scored project list, entries Z01, Z02, Z03, Z07, Z09, Z11, Z15, Z16, Z19, N02, N03, N04, plus two new chapters | `../../03_Job_Fit_Assessments/Fraunhofer_IIS_mioty/bench_projects/bench_projects_v2.md` |
| Appendix A | Every Z, N and B entry that did not become a chapter, keeping its original number and score | same |
| Appendix D | The workbook's German and English glossary, extended | the workbook |

The scored list is a ranking document, not a build manual. This volume is the
build manual. Chapters 19 and 20 are new: the cache and memory-protection
chapter because the family's worst trap deserves one, and the RTOS chapter
because a job-fit note records "no shipped RTOS application" as the standing gap
and says one exercise on the board he already owns would close it.

---

## 2. Licence categories

Applied consistently, and stated in each chapter rather than only here, because
a reader has to know before writing code around a dependency.

**A. Safe to depend on and to vendor.** Most of what carries this book. Note
that the vendor's *basic examples* for this board are BSD-3-Clause even though
its middleware is not, so the three-way example tree that inspired the variants
format can be adapted, not merely admired.

**B. Safe to depend on, not to copy.** The Python interface to the power
instrument is GPL-2.0. Install it from its package index; do not fork it into a
portfolio repository.

**C. Read, do not copy.** The vendor's middleware and example *projects* under
SLA0044 and SLA0047, redistributable only with that vendor's silicon. The
sensor-fusion middleware ships as pre-compiled objects under SLA0077. The
machine-learning generator's output explicitly may not be placed under open
source terms. One production project is GPL-3.0 and its engineering insight is
worth reading; its code must not be pasted anywhere permissive.

**D. Recommend, never quote at length.** Teaching material whose licence forbids
reproduction in a commercial work: one widely recommended video course's code is
AGPL-3.0, and one university textbook is free for personal, non-commercial use
only. At the other end, one free course releases code under 0BSD and slides
under CC BY 4.0, which makes it the safest thing to build an exercise on.

**E. No licence file at all.** Several community repositories. That means all
rights reserved. Cite as reading; never vendor. Named individually below.

---

## 3. The pool, by chapter

### Chapters 1 to 7, the workbook

**Chapter 1, toolchain and first light.**
Sergey Lyubka's bare-metal programming guide, MIT, the most complete open
walkthrough of vector table, startup and linker script:
`https://github.com/cpq/bare-metal-programming-guide`. Its templates stop at
Cortex-M33 and M4 parts, so the clock tree, the caches and the memory regions
here are the author's work. For the build, either Konstantin Oblaukhov's CMake
package layer, MIT, which supports the H7 family
(`https://github.com/ObKo/stm32-cmake`), or Tilen Majerle's Cube and CMake and
editor template, MIT (`https://github.com/MaJerle/stm32-cube-cmake-vscode`).
The vendor has supported CMake generation since version 6.11 of its configurator,
which is worth a section on tool-owned against author-owned files.
Register-level skeletons for this exact board:
`https://github.com/Chalandi/Blinky_Nucleo_H7A3` (brings the part to 280 MHz with
the right wait states; **licence not confirmed, check before reuse**) and
`https://github.com/kodezine/NUCLEO-H7A3ZI-Q` (CMake with the LLVM-based
toolchain; **no licence stated**). Note the toolchain rename: the LLVM Embedded
Toolchain for Arm ended at 19.1.5 and is superseded by Arm Toolchain for
Embedded at `https://github.com/arm/arm-toolchain`.

**Chapter 1, printf.** Memfault's article is the definitive treatment, covering
the retarget hook, the two C library variants, reentrancy and the flash cost:
`https://interrupt.memfault.com/blog/printf-on-embedded`. Shawn Hymel's is the
concrete UART version: `https://shawnhymel.com/1873/how-to-use-printf-on-stm32/`.
The fastest alternative, SEGGER's real-time transfer, is licensed for use only
with that vendor's probe, so it is named and not used.

**Chapter 2, ring buffer.** Majerle's lightweight ring buffer is the practical
base, MIT, with zero-copy regions built for DMA:
`https://github.com/MaJerle/lwrb`. Quantum Leaps' smaller buffer, MIT, carries a
test suite and states its atomicity assumptions explicitly:
`https://github.com/QuantumLeaps/lock-free-ring-buffer`. Both assume a coherent
view of memory; the cache question is chapter 19.
The academic anchor, and it is a good one: Lê, Guatto, Cohen and Pop, "Correct
and Efficient Bounded FIFO Queues", SBAC-PAD 2013, which revisits the classic
single-producer queue under the C11 memory model and gives the precise
acquire and release placement: `https://www.irif.fr/~guatto/publications/sbac13.pdf`.
For the barriers themselves, Arm's application note DAI0321A on memory barrier
instructions for M-profile.

**Chapter 3, interrupt receive.** There is no clean reference implementation.
What exists is a well documented set of failure modes in the vendor's own
community: the receive call that disables its own interrupt on completion and
loses bytes, and the overrun path that returns to ready without clearing the
interrupt, producing a storm. This chapter reproduces the documented bug and then
fixes it, which is a better chapter than one that starts from working code.
`https://github.com/controllerstech/stm32-uart-ring-buffer` has **no licence
file**: read it, do not vendor it.

**Chapter 4, DMA and the idle line.** The canonical reference, MIT, with working
examples across eight families including an H7:
`https://github.com/MaJerle/stm32-usart-uart-dma-rx-tx`. No example for this
exact board, and **no cache handling**: with the data cache on and the buffer in
main memory the invalidate step is the author's, which points at chapter 19.

**Chapter 5, framing and CRC.** Majerle's lightweight packet library, MIT, is
close to the whole chapter in a box, with addressing, command field, flags and a
selectable checksum: `https://github.com/MaJerle/lwpkt`. It offers 8-bit and
32-bit checksums but **not 16-bit**, so the 16-bit variant and its routing to the
hardware unit is the work. It depends on the ring buffer of chapter 2, which is a
pleasing link. Craig McQueen's byte stuffing library is the best of its kind,
MIT: `https://github.com/cmcqueen/cobs-c`. The vendor's application note AN4187
documents the checksum peripheral's reversal and initial-value settings that must
be right for hardware to agree with a software reference, a known trap with a
trail of community threads behind it **[browser-check]**.

**Chapter 6, timer sampling.** Ian Ross's five-step progression on a Cortex-M7
Nucleo is exactly this chapter's structure, MIT, though small and unmaintained:
`https://github.com/ian-ross/stm32-timer-adc-dma`, with the written series at
`https://www.skybluetrades.net/blog/2020/11/2020-11-26-stm32-timer-adc-dma-3/`.

**Chapter 7, low power.** The vendor's firmware package carries **three power
examples for this exact board**: standby, standby with clock wake, and the deeper
stop mode with clock wake, under `Projects/NUCLEO-H7A3ZI-Q/Examples/PWR/`. For
the instrument, the Python interface `https://github.com/IRNAS/ppk2-api-python`
is **GPL-2.0**: install from its package index, do not vendor. Method:
`https://blog.golioth.io/measuring-current-consumption-with-power-profiler-kit-ii/`.
**The H7-specific trap**: current after wake stays high because the clock tree is
not restored; the phase-locked loop does not come back on its own.
**No peer-reviewed method for this instrument exists**, so the measurement rigour
is original work.

### Chapters 8 to 12, the node

**Chapter 8.** No prior art worth the name. The state machine is the author's.

**Chapter 9, codec.** Smallest encoder, CC0, effectively public domain:
`https://github.com/bergzand/NanoCBOR`. Schema-driven generator used in
production, Apache-2.0: `https://github.com/NordicSemiconductor/zcbor`.
Minimalist alternative, MIT: `https://github.com/intel/tinycbor`. For bit
packing, `https://github.com/CrustyAuklet/bitpacker` shares format strings with a
Python counterpart, which is elegant, but it is **C++14 where this book is C**;
say so rather than paper over it.

**Chapter 10, energy accounting.** The method from chapter 7 plus the published
empirical work on cellular module energy, which is the model for presenting
per-state traces, and the lifetime estimation method from the sensor literature.
The phase splitting by marker pin is the author's.

**Chapter 11, AT engine.** Majerle's cellular library, MIT, explicitly supports
the SIM7070G on this bench and bundles a messaging client:
`https://github.com/MaJerle/lwcell`. **It requires an operating system and has no
bare-metal path**, which is precisely the gap this chapter fills and why chapter
20 exists. It does **not** list the SIM7600E-H or SIM7020E. The vendor's own
cellular middleware covers none of the three modems on this bench, targeting a
different set of vendors entirely. The narrow-band modem has **no
point-to-point protocol**, so this is necessarily a command-set chapter.
A peer-reviewed empirical study of that exact modem gives realistic signal
expectations for the retry logic, though its host is a single-board computer:
`https://jtde.telsoc.org/index.php/jtde/article/view/955`.

**Chapter 12, test rig.** Host test tooling, all MIT or BSD:
`https://github.com/ThrowTheSwitch/Unity`,
`https://github.com/ThrowTheSwitch/Ceedling` (needs Ruby, a real friction point
to call out), `https://github.com/cpputest/cpputest` (memory-leak detection
between setup and teardown), `https://github.com/meekrosoft/fff` (header-only,
the lightest fit for C; **its default history silently drops calls past ten
arguments and seventeen calls**, which is a trap worth documenting). Memfault
compares them directly at
`https://interrupt.memfault.com/blog/unit-test-mocking`.
For the rig, Golioth's series, especially
`https://blog.golioth.io/automated-hardware-testing-using-pytest/`. The
architectural point every source agrees on: build on cloud runners, publish the
binary as an artifact, then have the self-hosted Raspberry Pi download, flash and
run. The tools are prior art; the architecture is not.
**Watchdogs**: the two-layer design, a software watchdog firing slightly before
the hardware one so a crash record survives, plus a task-liveness bitmask, is at
`https://interrupt.memfault.com/blog/firmware-watchdog-best-practices`. Jack
Ganssle's "Great Watchdogs" is still the best on why naive watchdogs fail:
`https://www.ganssle.com/watchdogs.htm`. The only shipping reference
implementation of per-task timeouts is the other operating system's task
watchdog, Apache-2.0:
`https://docs.zephyrproject.org/latest/services/task_wdt/index.html`.
**The common kernel ships no task-watchdog abstraction at all**, and nobody has
written that comparison.

### Chapters 13 to 16, the shields

**The register-level driver collection is the spine**, BSD-3-Clause, covering
every sensor on both shields behind a two-function porting shim:
`https://github.com/STMicroelectronics/STMems_Standard_C_drivers`. It gives no
board layer, no orchestration of the bus topologies and no build system, which is
the right amount of "not given" for a chapter.
The vendor's expansion package is **licence-flagged**: its fusion middleware
ships as pre-compiled objects under SLA0077 and must not be vendored.
The other operating system's in-tree shield definitions document the three bus
topologies more clearly than the vendor's own manual:
`https://docs.zephyrproject.org/latest/boards/shields/x_nucleo_iks4a1/doc/index.html`.

**The IKS5A1 contradiction, settled.** Four bench documents disagreed and the
sibling volume records it as unresolvable. It is resolvable. Four independent
sources agree, the vendor's product page among them: the fitted parts are
**ISM6HG256X, ISM330IS with ISPU, IIS2DULPX, IIS2MDC and ILPS22QS**. The
LSM6DSV32X is a consumer part on an industrial shield and reaches these boards
only through an adapter kit. The LSM6DSO16IS is the *other* shield's ISPU part,
which is the likely source of the confusion since the two are siblings with the
same core and different qualification. **This also needs a correction note in the
sibling volume**, whose chapter 12 and inventory table both carry the wrong list.

**In-sensor intelligence.** Ready-made classifier configurations, BSD-3-Clause:
`https://github.com/STMicroelectronics/st-mems-machine-learning-core`. State
machine programs, BSD-3-Clause:
`https://github.com/STMicroelectronics/st-mems-finite-state-machine`. Note both
replaced predecessors that were discontinued in July 2025 and that much existing
writing still points at the old addresses. The in-sensor processor examples are at
`https://github.com/STMicroelectronics/st-mems-ispu`, which **declares no licence
at its root**: check files individually. It ships prebuilt outputs so a demo runs
before the toolchain is installed, which is a good chapter opening. Independent
writing about it is almost non-existent, which the chapter says.

**Time of flight.** The redistributable path is the vendor's own Arduino
organisation, which republishes the driver under **BSD-3-Clause** unlike the
version behind the click-through on the vendor's site:
`https://github.com/stm32duino/VL53L8CX`. Strip the wrapper, keep the copyright
headers, publish the port. The hook is that this sensor has volatile memory only,
so an **84 kilobyte firmware image crosses the bus at every power-on** and takes
seconds, which is a real design constraint and a natural way into fast-mode bus
work. Background on what the internal core actually is:
`https://the6p4c.github.io/2022/06/13/stxp70-sthorm-p2012.html`.

**Chapter 16, placement.** One recent preprint measures two of the three
placements and reaches a counter-intuitive result: the host wins, because cost is
dominated by the three-transaction bus read rather than by classification:
`https://arxiv.org/abs/2606.00524`. It is **unreviewed, its authors are
unaffiliated on the page, and its host is a far heavier machine than this one**.
Treat it as the hypothesis to test. A microcontroller host with a much shorter
wake-up could well flip it.

### Chapters 17 to 20, inside the core

**Chapter 17, abstraction layers.** The only measured comparison worth the name
is one engineer's bench work on an H7 at 550 MHz, finding the vendor toggle call
running at a fraction of the direct register write:
`https://metebalci.com/blog/stm32h7-gpio-toggling/`. The vendor's firmware
package carries parallel example trees at both levels for this board, which is
the raw material. **This is the weakest-evidenced topic in the book**: plenty of
opinion, almost no reproducible measurement, and no well-argued piece from the
vendor-layer side. The chapter says so and supplies the measurement.
For the Rust variant: hardware crate `https://github.com/stm32-rs/stm32h7xx-hal`
(0BSD; its `stm32h7a3` feature routes to the `stm32h7b3` peripheral module and
selects `rm0455`, which is the right engineering call and worth explaining);
peripheral crate `https://github.com/stm32-rs/stm32-rs`; async framework
`https://github.com/embassy-rs/embassy` (Apache-2.0 or MIT, names
`stm32h7a3zi` exactly, **no worked example for this part**, only for its twin);
real-time framework `https://github.com/rtic-rs/rtic`; flashing
`https://github.com/probe-rs/probe-rs`, whose target manifest carries
`STM32H7A3ZITxQ`.

**Chapter 18, transforms.** `https://github.com/ARM-software/CMSIS-DSP`,
Apache-2.0, entirely clean. The verification angle is better supported than
expected: the maintainer ships a Python wrapper whose API mirrors the C API, so
the same test vectors run both sides, and wrote the learning path that builds a
pipeline against a reference implementation:
`https://learn.arm.com/learning-paths/embedded-and-microcontrollers/cmsisdsp-dev-with-python/`.
The vendor's application note on system architecture and performance contains
exactly the benchmark this chapter wants, one transform run with code and data in
each memory, cache on and off, with numbers **[browser-check]**; its companion
package ships the linker scripts. **It does not cover this part**, so its numbers
are the shape of the answer, not the answer.
**No rigorous float32 against Q15 comparison exists for this core.** The
widely-repeated figure is a Cortex-M4 result and does not transfer. On this part
memory placement will probably matter more than numeric format, which is itself
the finding.

**Chapter 19, caches and coherency.** The richest prior art in the book. The
vendor's application note AN4839 on the L1 cache, AN4838 on the memory protection
unit, and AN5212 on using the cache for performance **[browser-check]**. A recent
community article by one of their engineers lays out exactly the three fixes with
complete configuration code:
`https://community.st.com/t5/stm32-mcus/how-to-use-the-memory-protection-unit-to-manage-cache-coherency/ta-p/858387`.
Their own write-up of the trap, which adds a fourth: a buffer left in tightly
coupled memory that the main engines cannot reach at all:
`https://community.st.com/t5/stm32-mcus/dma-is-not-working-on-stm32h7-devices/ta-p/49498`.
Three application briefs from another vendor map one to one onto the three fixes
and are clearer than the originals. The best teaching treatment is a three-part
written series: `https://blog.feabhas.com/2020/10/introduction-to-the-arm-cortex-m7-cache-part-1-cache-basics/`.
One video does this on an H7 with a non-cacheable region and a custom linker
section: `https://www.youtube.com/watch?v=_K3GvQkyarg`.
Evidence it is real rather than theoretical: an open bug in the vendor's own
examples, `https://github.com/STMicroelectronics/STM32CubeH7/issues/153`, and the
same bug in two major projects.
**No maintained permissive library offers a declarative region table with cache
attributes.** That is a hole this chapter could fill.

**Chapter 20, RTOS.** Kernel and its free official book, MIT:
`https://github.com/FreeRTOS/FreeRTOS-Kernel` and
`https://github.com/FreeRTOS/FreeRTOS-Kernel-Book`. The other operating system
has an **upstream in-tree port for this exact board**, Apache-2.0, which makes
the comparison cheap:
`https://docs.zephyrproject.org/latest/boards/st/nucleo_h7a3zi_q/doc/index.html`.
The other commercial kernel is supported at silicon level but **ships no example
for this board**, so it is named and not used.
**A number to kill.** The most repeated claim in this field is that waking a task
by direct notification is forty-five percent faster than by semaphore. It comes
from a vendor tutorial page stating **no processor, no compiler, no optimisation
level and no method**, and the kernel's own official book has dropped the figure,
now saying only "significantly faster". The hard, current, falsifiable number is
the memory cost: **five bytes per task**. The speed claim becomes an exercise.
Also worth stating: this kernel's priority inheritance is a basic form assuming
one mutex at a time, is not transitive, and there is no priority ceiling.
For measurement: `https://github.com/GorgonMeducer/perf_counter`, Apache-2.0,
which deliberately uses the system tick rather than the cycle counter because the
former is universally present. Method to copy, reporting median, 99th percentile
and maximum: the two-part study at
`https://www.ul.com/sis/blog/measuring-real-time-operating-system-performance-part-ii-comparing-freertos-vs-zephyr`.
**Its platform is a Cortex-M4 at 80 MHz, not this core**, so its numbers do not
transfer, only its method.
The active-object argument: `https://www.state-machine.com/products/qp`,
**GPL or commercial, safety editions commercial only**, and the free video course
at `https://github.com/QuantumLeaps/modern-embedded-programming-course` is
**AGPL-3.0**, the most restrictive thing in this pool. The counter-argument, that
this compares a badly used kernel against a well used framework, appears in the
comment threads on the author's own articles. Present both, and implement the
pattern twice: by hand on ordinary queues and on the framework. **Nobody has
published that comparison.**

---

## 4. Reach and intelligence

**The cleanest path needs no network stack on the board at all.** The radio
coprocessor's firmware exposes messaging commands directly, so the board writes
a command over a serial link and the coprocessor does the transport and the
encryption: `https://github.com/espressif/esp-at`, Apache-2.0. The best
independent host-side parser, MIT, bundles a messaging client:
`https://github.com/MaJerle/lwesp`. Three honest layers, in rising order of code
on the board: let the coprocessor own the protocol; run a transport-agnostic
client over a byte stream it provides (`https://github.com/FreeRTOS/coreMQTT`,
MIT, MQTT 5.0, no transport assumed); or run a full network stack on the board,
which **fits comfortably in the memory available and is still the wrong answer**
for a host twenty centimetres away. It earns its keep only on the cellular link.
Note one client is GPL-3.0 with a commercial option, which is a trap for a reader
shipping a product.

For a framed link with a real host daemon and a code generator, rather than
inventing a protocol: `https://github.com/EmbeddedRPC/erpc`, BSD-3-Clause, and
`https://github.com/micro-ROS/micro_ros_stm32cubemx_utils`, Apache-2.0. For the
payload inside the frame, `https://github.com/nanopb/nanopb`, Zlib.

**On intelligence the framing inverts.** With this much memory the part sits at
the **large end** of the tiny machine learning envelope and memory is almost
never the binding constraint. The reference anomaly-detection model from the
standard suite is about 270 kB and fits several times over. What binds is
throughput and energy: one core, no accelerator, and **no vector extension**.

**A correction that cuts the other way**: this core *does* have the
signal-processing instruction extension, so the optimised neural kernels do help
here, through that path rather than through vectors:
`https://github.com/ARM-software/CMSIS-NN`, Apache-2.0. There is even a build
setting recommended specifically for this core. Every impressive vendor speed-up
figure belongs to later cores; do not let a chapter imply otherwise.

So: classification and anomaly detection at sensor rates, yes. Continuous audio,
yes but measure it. Camera-rate vision, marginal. Anything transformer-scale or
on-device training, no.

**The thesis worth measuring**, and it should be a chapter's argument rather
than a footnote: for most sensor problems on a part like this, a transform plus a
statistical model beats a neural network on accuracy per kilobyte, is
deterministic in latency, and can be explained to a customer. Two libraries carry
it: `https://github.com/emlearn/emlearn`, MIT, which does decision trees, naive
Bayes and anomaly detection from 2 kB of flash, and CMSIS-DSP. One peer-reviewed
paper argues it directly: `https://arxiv.org/pdf/2107.09448`.
Also permissive and useful: `https://github.com/xioTechnologies/Fusion`, MIT, the
current home of the well-known orientation filter.

**Licence caution on the tooling.** The popular training service treats this part
as a **porting target, not a supported board**: no ready-made firmware, no data
forwarder, no official support, only the portable export path. Its exported kit
declares **no single machine-readable licence**, one optimising component is
proprietary and another is sold only at the top tier. The runtime with no licence
problem is `https://github.com/tensorflow/tflite-micro`, Apache-2.0, which is a
real argument for making it the book's primary path.

---

## 5. Roadmap material

The last section of each chapter draws on these, and never invents a next step.

- The community roadmap, which distinguishes firmware from embedded Linux from
  hardware and says which to weight:
  `https://github.com/m3y54m/Embedded-Engineering-Roadmap`, with the essay at
  `https://interrupt.memfault.com/blog/embedded-systems-roadmap-bridging-the-gap`.
  Its central advice, that projects beat reading, is this book's thesis too.
- The free open textbook that grew out of a university course and an online
  certificate, which is the closest thing to a canonical curriculum for the
  machine-learning chapters: `https://mlsysbook.ai/`.
- The free-to-audit course on embedded machine learning, taught by the author of
  several of the video series cited above:
  `https://www.coursera.org/learn/introduction-to-embedded-machine-learning`.
- The best free course on the other operating system, from a different silicon
  vendor, whose concepts transfer directly:
  `https://academy.nordicsemi.com/courses/nrf-connect-sdk-fundamentals/`.
- The free course that walks one problem from superloop to event-driven
  architecture across fifty-six lessons, **code AGPL-3.0, recommend but never
  quote**: `https://www.state-machine.com/video-course`.
- The free course whose code is 0BSD and slides CC BY 4.0, the safest to build an
  exercise on, though its examples are not on this board:
  `https://github.com/ShawnHymel/introduction-to-rtos`.
- The university specialisation that actually teaches the mathematics of
  real-time scheduling, which almost nothing else does:
  `https://www.coursera.org/specializations/real-time-embedded-systems`.
- The most recommended paid course on this kernel and this silicon family:
  `https://www.udemy.com/course/mastering-rtos-hands-on-with-freertos-arduino-and-stm32fx/`.
- The vendor's own free training modules, which cover this family well although
  there is **no course specific to this part** **[browser-check]**.
- Standards a reader will eventually meet: the consumer device security baseline,
  the industrial component security series, the two evaluation schemes, and the
  coding standard that matters once safety enters.

---

## 6. Gaps: where this book would produce results that do not exist

Each falls inside a chapter already planned. State them as new work, not as a
survey.

1. **No thesis or paper programs these in-sensor classifiers specifically.**
   Plenty on activity recognition generally, nothing on this engine.
2. **No rigorous comparison of the two numeric formats for transforms on this
   core.** The repeated figure is from a smaller, slower core.
3. **No peer-reviewed work measures all three placements of a classifier** on one
   task on one set of hardware. One preprint does two of three.
4. **No published benchmark result for this core on the standard tiny machine
   learning suite.** The suite's reference baseline is a smaller core; the recent
   vendor submissions are a different core and an accelerated preview part; the
   vendor's harness for running it has been dormant since April 2024.
5. **No rigorous comparison of the execution models.** Nobody has measured six
   ways of solving one problem on one board with a stated method.
6. **Neither operating system publishes a mapping of its synchronisation
   primitives to the other's**, and the third-party attempts are partial.
7. **No published measurement of priority-inversion blocking on this core.**
8. **Nobody has implemented the active-object pattern twice** and compared
   honestly.
9. **No maintained permissive library offers a declarative memory-protection
   region table** with cache attributes.
10. **No peer-reviewed method exists for the power instrument on this bench**, so
    the measurement rigour in chapters 7, 10 and 12 is original.

Also worth recording as a negative result: **no thesis or paper builds a
low-power sensor node on this family at all.** That literature runs on the
ultra-low-power families, which idle in microamps where this part idles in
milliamps. The bench's own German inventory note already said a board with its
debugger attached is not a frugal node. So Part II keeps its subject and changes
its claim: not "here is a low-power node" but "here is how you account for
energy, and what racing to sleep looks like on a part chosen for speed", with the
low-power families named as the comparison to beat.

---

## 7. Method notes for whoever continues this

- **When a claim rests on a large generated file, search the file, not a summary
  of it.** One sweep reported that the flashing tool had no definition for this
  part and it was written into the plan as fact. A later sweep searched the raw
  manifest and found fifty-one entries including the exact die. Readers act on
  claims about tool support.
- **Two independent reads of the same document can disagree.** Two sweeps read
  the same upstream board description and reported opposite answers on whether
  this part has an Ethernet controller. That is now item 1 of the confirm list.
- **A repository with no licence file is not open source.** Default copyright
  applies. Three vibration-analysis repositories that look exactly like chapter
  material were all created within a month of this research, have no users, and
  two have no licence. Read them as worked examples; re-derive rather than copy.
- **Prefer the project that names your exact part over the one that names your
  family.** That distinction separated the useful results from the misleading
  ones throughout this research, and it is the same distinction the front-matter
  warning is about.
