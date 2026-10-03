# The code for the twenty projects

The software for the twenty projects of this volume. The book and the code live
in one repository: `chapters/` and `figures/` are the written volume,
`projects/`, `shared/`, `python/firmkit/` and `python/tests/` are what runs.

Chapter NN of the book is the written design for project PNN. Start from
[the contents](../CONTENTS.md).

**Eight of the twenty have code.** 87 checks pass on a laptop with no board, no
probe and no Raspberry Pi. The remaining twelve need hardware, and nothing in them
is written yet rather than written and untested.

**Three states, and the difference between them matters.** Until Friday 2 October
2026 this page had only two, because nothing had ever been cross-compiled, and the
distinction was not yet needed. It is now:

| State | What it means |
| --- | --- |
| **runs on the board** | cross-compiled, flashed, and observed working on the hardware. P01, P02 and P09 |
| **links** | cross-compiles and links clean for the target, and has never been flashed, so nothing is known about its behaviour |
| **host only** | proven on a laptop against synthetic input, and never built for the target at all |

The middle state is the one worth being careful about. A target binary that links is
evidence about the build and about nothing else. P06's three back ends and P09's
codec both reached that state on Friday 2 October 2026 and neither has been run.

| Project | What it is | State |
| --- | --- | --- |
| [P01](P01-toolchain-first-light/) | The toolchain, startup and linker script every other project needs | **runs on the board**, Friday 2 October 2026. LD1 green on PB0 blinks at 499.7 ms, `printf` reaches COM13 at 115200, the user button on PC13 reads, and the report carries the core, AHB and APB1 clocks decoded from RCC at startup. The delay loop measures itself against `DWT_CYCCNT` every boot, 9 cycles per iteration, and a 100 ms request lands within 20 parts per million. The oscillator measured 64.17 to 64.18 MHz across six reductions. Still refused: the 280 MHz tree, which needs RM0455 for the PLL fields, the flash latency and the voltage scaling, in that order |
| [P02](P02-ring-buffer/) | A single producer, single consumer ring buffer | **runs on the board**, four targets one per ordering mode, producer in thread mode and consumer in SysTick. Re-measured Saturday 3 October 2026 with the instruction cache on, which overturned the first table: cold, the DMB build measured 2 cycles FASTER than no barrier at all, so that comparison had the sign wrong, not merely the magnitude. On the cached figures the compiler fence costs 0.00 cycles and 0 bytes while still emitting different instructions (same length, different SHA-256), one DMB in put and get costs 22.00 cycles and 16 bytes, and acquire plus release costs 29.00 and 24. The earlier claims of 7 and 17 cycles are withdrawn. Zero mismatches in 21.4 million bytes across the four. Also host tested in all four modes, 21.6 million bytes per run |
| [P03](P03-interrupt-receive/) | Receiving on interrupt without losing bytes | host only. The measurement is proven, attributing every lost byte to the target or the bridge. `rx_ring.c` and both documented failure modes are written and have never been built for the target |
| [P04](P04-dma-idle-line/) | Circular DMA and the idle line | not started. Needs a transfer engine |
| [P05](P05-framing-crc/) | Framing and the hardware CRC unit | host only, and proven there: both implementations reach the published check value 0x29B1, and every single-bit corruption is rejected. The peripheral half waits on RM0455 |
| [P06](P06-timer-sampling/) | Sampling on a timer at exactly 1 kHz | **links**, all three back ends, since Friday 2 October 2026, and none has been flashed. The witness is proven on synthetic input on the host. Each back end still refuses at run time rather than guessing a converter, timer or transfer engine setting RM0455 governs. The cycle counter it needs now exists and works, which was the first of its dependencies to be settled |
| [P07](P07-stop-mode/) | Stop mode, RTC wake, and a battery number | not started. Needs the PPK2 and a running clock tree |
| [P08](P08-node-state-machine/) | The node's state machine, transmit as a stub | written and proven: all 18 transition rows reachable, and the stub payload matches P09's encoder byte for byte |
| [P09](P09-payload-codec/) | The payload codec and its Python twin | **runs on the board**, Saturday 3 October 2026. The negative golden vector, feature -1, encodes to 2405FFFFE8 on the Cortex-M7, byte for byte what the host produces, and decodes back to the value it started from, so sign extension, bit order and field packing agree between the two compilers. It also carries the volume's measurement of placement: four images displaced by 0, 16, 32 and 48 bytes and otherwise identical, where the cold cost tracks the offset within a 32 byte granule (3220.99 cycles at offset 0, 3163.98 at offset 16, the two images at each offset agreeing to the hundredth) and the cached cost is 2378.99 in all four. Host half complete: three implementations agree over 100000 cases |
| [P10](P10-energy-phases/) | Where the energy goes, by phase | not started. Needs the PPK2 and marker pins |
| [P11](P11-at-engine/) | An AT engine that never blocks | not started. Needs the SIM7020E |
| [P12](P12-energy-regression/) | Energy as a regression test, and the rig | both gates written and proven: a five percent charge regression turns the build red with nobody at the bench. The hardware job is not built |
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
tested by introducing defects on purpose. None of it has run on hardware. **No figure
anywhere in this repository is a measurement of anything physical.** Where a number
appears it is either arithmetic, a count of synthetic cases, or a host object size
that is explicitly not the flash cost on the board.

## Why there are shared directories

Most of these projects are the same shape underneath. P03, P04, P05 and P11 all
move bytes through one ring buffer. P08 builds the frame P09 encodes, P12 tests
it on a runner with no board, and P11 sends it over a modem. P02, P06, P17, P18
and P20 all want the cycle counter. Those parts are not properties of whichever
project first needed them, so they live together.

| Path | What it is |
| --- | --- |
| `c/ring/ring.{c,h}`, `c/ring/barrier.h` | P02's structure and its four ordering modes, used by P03, P04, P05, P11 |
| `c/payload/payload.{c,h}`, `c/payload/payload_fields.h` | P09's codec, used by P08 and P12. The header is generated |
| `c/instr/` | the cycle counter and the gated frequency counter, the instruments the board provides about itself |
| `python/firmkit/payload.py` | the Python twin: test oracle, edge-host decoder, and the same file again on the board under MicroPython |
| `python/firmkit/rate.py` | P06's edge and rate analysis, two independent routes that must agree |
| `python/firmkit/cbor.py` | the serialisation-size arithmetic, so a format decision carries a number |
| `projects/PNN-name/` | each project: its own specification, variants, target code, design notes and README |
| `python/tests/` | every suite, central, none of which touch a device |
| `python/tools/` | the generator, the host build, and the checks that prove the checks work |

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
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python python/tools/build_host.py
```

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python -m pytest tests -q
```

Thirty-five checks, about nine seconds, on a machine with no board attached.
Each project's own README gives its flow.

`.venv` is ignored by git, so it is built from `requirements.txt` rather than
committed.

> **If the build fails with the library open.** Windows will not let a loaded
> library be overwritten, so `python/tools/build_host.py` refuses and says which file,
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
flashing tool, and `ninja`. Until then `python/tools/build_host.py` calls the compiler
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

Nothing in `python/firmkit/` or `python/tests/` imports anything from the second file. If a
module there ever needs one, that module is in the wrong half of the project: it
needs a device, so it belongs in a project directory behind a command the suite
does not call. `P09/listen.py` is the example, and it guards its import and
refuses cleanly rather than failing at the point of use.
