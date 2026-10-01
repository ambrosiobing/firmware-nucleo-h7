# The code for the twenty projects

The software for the twenty projects of this volume. The book and the code live
in one repository: `chapters/` and `figures/` are the written volume,
`projects/`, `shared/`, `firmkit/` and `tests/` are what runs.

Chapter NN of the book is the written design for project PNN. Start from
[the contents](../CONTENTS.md).

**Three projects are written so far.** The rest arrive as the toolchain and the
wiring do. Every one of the twenty has a directory from the start, so the shape
of the volume is visible here rather than appearing a project at a time.

| Project | What it is | State |
| --- | --- | --- |
| [P01](projects/P01-toolchain-first-light/) | The toolchain, startup and linker script every other project needs | not started, and it blocks every target build |
| [P02](projects/P02-ring-buffer/) | A single producer, single consumer ring buffer | written, host tested, never on the target |
| [P03](projects/P03-interrupt-receive/) | Receiving on interrupt without losing bytes | not started |
| [P04](projects/P04-dma-idle-line/) | Circular DMA and the idle line | not started |
| [P05](projects/P05-framing-crc/) | Framing and the hardware CRC unit | not started |
| [P06](projects/P06-timer-sampling/) | Sampling on a timer at exactly 1 kHz | witness written and proven, firmware never compiled |
| [P07](projects/P07-stop-mode/) | Stop mode, RTC wake, and a battery number | not started |
| [P08](projects/P08-node-state-machine/) | The node's state machine, transmit as a stub | not started |
| [P09](projects/P09-payload-codec/) | The payload codec and its Python twin | written and proven, host half complete |
| [P10](projects/P10-energy-phases/) | Where the energy goes, by phase | not started |
| [P11](projects/P11-at-engine/) | An AT engine that never blocks | not started |
| [P12](projects/P12-energy-regression/) | Energy as a regression test, and the rig | not started |
| [P13](projects/P13-iks4a1-fifo/) | The IKS4A1: FIFO, watermark, interrupt | not started |
| [P14](projects/P14-iks5a1-dual-range/) | The IKS5A1: low and high g at once | not started |
| [P15](projects/P15-time-of-flight/) | Eight by eight time of flight on the MCU | not started |
| [P16](projects/P16-classifier-placement/) | Where the classifier runs: sensor, MCU or host | not started |
| [P17](projects/P17-hal-against-registers/) | The same driver twice | not started |
| [P18](projects/P18-transforms-filters/) | Transforms with a numerical acceptance test | not started |
| [P19](projects/P19-caches-mpu-dma/) | Caches, the MPU, and stale DMA reads | not started |
| [P20](projects/P20-rtos-task-set/) | Three jobs at once, as an RTOS application | not started |

## Why there are shared directories

Most of these projects are the same shape underneath. P03, P04, P05 and P11 all
move bytes through one ring buffer. P08 builds the frame P09 encodes, P12 tests
it on a runner with no board, and P11 sends it over a modem. P02, P06, P17, P18
and P20 all want the cycle counter. Those parts are not properties of whichever
project first needed them, so they live together.

| Path | What it is |
| --- | --- |
| `shared/ring.{c,h}`, `shared/barrier.h` | P02's structure and its four ordering modes, used by P03, P04, P05, P11 |
| `shared/payload.{c,h}`, `shared/payload_fields.h` | P09's codec, used by P08 and P12. The header is generated |
| `shared/instr/` | the cycle counter and the gated frequency counter, the instruments the board provides about itself |
| `firmkit/payload.py` | the Python twin: test oracle, edge-host decoder, and the same file again on the board under MicroPython |
| `firmkit/rate.py` | P06's edge and rate analysis, two independent routes that must agree |
| `firmkit/cbor.py` | the serialisation-size arithmetic, so a format decision carries a number |
| `projects/PNN-name/` | each project: its own specification, variants, target code, design notes and README |
| `tests/` | every suite, central, none of which touch a device |
| `tools/` | the generator, the host build, and the checks that prove the checks work |

## Three rules this repository obeys

**Nothing here needs hardware.** The whole suite runs on a laptop with no board,
no probe and no Raspberry Pi, and the CI runner is therefore a fair test of it
rather than a reduced one. If something needs a device it sits behind a command
the suite does not call, and its project README says so.

**A refusal beats a guess.** Where a value is not confirmed against RM0455, the
code returns an error rather than running with a plausible setting. P06's three
acquisition back ends all do this today. A board that runs and lies is worse than
one that refuses, and a clean square wave at the wrong rate is indistinguishable
from a right one without an external witness.

**Every measurement names its instrument, or says it was not taken.** A budget row
reading "not measured" is correct and finished. A plausible number is neither. No
figure here is a measurement of anything physical yet, because nothing here has
run on hardware.

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
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python tools/build_host.py
```

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python -m pytest tests -q
```

Thirty-five checks, about nine seconds, on a machine with no board attached.
Each project's own README gives its flow.

`.venv` is ignored by git, so it is built from `requirements.txt` rather than
committed.

> **If the build fails with the library open.** Windows will not let a loaded
> library be overwritten, so `tools/build_host.py` refuses and says which file,
> rather than letting the linker produce an error that does not mention locking.
> It is nearly always a `pytest` run in another window or a Python session that
> imported the library through `ctypes`. Close it and run the build again.
> Nothing is wrong with the source.

## What blocks the target half of all twenty

On **win11 aquamarine**, the authoring laptop, re-checked **Thursday 1 October
2026**: CMake 4.4.2 and Python 3.12.10 are installed. There is no
`arm-none-eabi-gcc`, no `clang`, no `openocd`, no `probe-rs`, no
`STM32_Programmer_CLI`, and none of the usual ST or Arm install directories
exists. **Also absent: `make` and `ninja`**, so a cross compiler alone would
still leave CMake with no generator.

The download list is therefore three items and not two: the compiler, the
flashing tool, and `ninja`. Until then `tools/build_host.py` calls the compiler
directly, which is why it exists and why it will be deleted when `ninja` arrives.

P01 then owns the toolchain file, the startup code and the linker script that
every other project depends on.

## Requirements

Python 3.12 and a C11 and C++17 compiler. Two requirement files, and the split
between them is the point rather than tidiness.

| File | What is in it | Installed by the CI runner |
|---|---|---|
| `requirements.txt` | `pytest`, and nothing else | yes |
| `requirements-hardware.txt` | `pyserial` for a serial link, and `daqhats` noted for the Raspberry Pi only | no |

Nothing in `firmkit/` or `tests/` imports anything from the second file. If a
module there ever needs one, that module is in the wrong half of the project: it
needs a device, so it belongs in a project directory behind a command the suite
does not call. `P09/listen.py` is the example, and it guards its import and
refuses cleanly rather than failing at the point of use.
