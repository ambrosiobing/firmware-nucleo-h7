#!/usr/bin/env python3
"""Write a README into every project directory that has not got one.

    python tools/make_stub_readmes.py

Each of the twenty projects gets a directory from the start, so the shape of the
volume is visible in the repository rather than arriving a chapter at a time. A
directory with nothing in it says nothing, so each carries a README naming what
the project is, what it needs on the bench, and what state it is in.

This writes only the stubs. A project that has real work in it has a README
written by hand, and this script refuses to overwrite one.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROJECTS = ROOT / "projects"

# number, slug, title, what it needs on the bench, the one thing it is about
SPEC = [
    ("01", "toolchain-first-light",
     "The toolchain, first light, and printf over the ST-LINK",
     "Nucleo alone",
     "The cross toolchain file, the startup code and the linker script that every "
     "other project here depends on. Nothing else can be flashed until this exists."),
    ("02", "ring-buffer",
     "A single producer, single consumer ring buffer",
     "Nucleo alone",
     "Written. The structure is in shared/ring.c because P03, P04, P05 and P11 all "
     "use it."),
    ("03", "interrupt-receive",
     "Receiving on interrupt without losing bytes",
     "Nucleo alone",
     "The receive interrupt as the producer for P02's ring, and the rate at which "
     "bytes begin to be lost, measured rather than asserted."),
    ("04", "dma-idle-line",
     "Circular DMA and the idle line",
     "Nucleo alone",
     "The producer becomes a transfer engine and the head comes from the transfer "
     "counter. The compiler-only barrier of P02 stops being sufficient here."),
    ("05", "framing-crc",
     "Framing and the hardware CRC unit",
     "Nucleo alone",
     "A length prefix against byte stuffing, and a 16-bit checksum from the "
     "peripheral that agrees with a software reference. P09's host listener waits "
     "on this."),
    ("06", "timer-sampling",
     "Sampling on a timer at exactly 1 kHz",
     "Nucleo plus the MCC 118 DAQ HAT on a Raspberry Pi 4",
     "Written, host half proven. Three acquisition mechanisms behind one header, "
     "and an external witness that decides which of them actually samples at the "
     "rate it claims."),
    ("07", "stop-mode",
     "Stop mode, RTC wake, and a battery number",
     "Nucleo plus the nRF PPK2",
     "Charge per wake cycle with a spread rather than an average current, and the "
     "trap that current after wake stays high because the clock tree is not "
     "restored."),
    ("08", "node-state-machine",
     "The node's state machine, with a transmit that is a stub",
     "Nucleo alone",
     "Every transition reachable in test, and a stub payload that must match "
     "P09's codec byte for byte. That cross-check is both projects' acceptance "
     "criterion."),
    ("09", "payload-codec",
     "The payload codec and its Python twin",
     "Nucleo plus a host",
     "Written and proven. One specification as data, three implementations "
     "generated or derived from it, and golden vectors computed by hand."),
    ("10", "energy-phases",
     "Where the energy goes: wake, sense, compute, send",
     "Nucleo plus the nRF PPK2",
     "One duty cycle split into labelled phases by marker pins, with the per-phase "
     "charge summing to the cycle total."),
    ("11", "at-engine",
     "An AT engine that never blocks",
     "Nucleo plus the SIM7020E",
     "A command queue with timeouts, unsolicited results and backoff, where a "
     "missing SIM stopping it cleanly is a pass."),
    ("12", "energy-regression",
     "Energy as a regression test, and the rig that runs it",
     "Nucleo plus a Raspberry Pi plus the nRF PPK2",
     "Host tests and a size gate on a runner with no board, then a hardware job "
     "that turns a deliberate charge regression red with nobody at the bench."),
    ("13", "iks4a1-fifo",
     "The IKS4A1: FIFO, watermark, interrupt",
     "Nucleo plus the X-NUCLEO-IKS4A1 shield",
     "A logger that never polls, driven by the sensor's own watermark interrupt, "
     "and the shield's three bus topologies."),
    ("14", "iks5a1-dual-range",
     "The IKS5A1: low and high g at once, wide pressure",
     "Nucleo plus the X-NUCLEO-IKS5A1 shield",
     "Both acceleration ranges in use simultaneously, which is what this shield is "
     "for, and a dual full-scale barometer."),
    ("15", "time-of-flight",
     "Eight by eight time of flight, decided on the MCU",
     "Nucleo plus the X-NUCLEO-53L8A1 shield",
     "Multizone ranging with the sensor firmware upload as an explicit bring-up "
     "step, and a zone policy with hysteresis."),
    ("16", "classifier-placement",
     "Where the classifier runs: sensor, MCU or host",
     "Nucleo plus the IKS4A1 plus a Raspberry Pi",
     "One labelled dataset and the same classification in three places, with "
     "latency, memory and charge per decision for each. No peer-reviewed work "
     "does this three-way comparison."),
    ("17", "hal-against-registers",
     "The same driver twice: vendor layer against registers",
     "Nucleo plus one shield",
     "Two implementations of one sensor read, compared on code size and cycles, "
     "with the cost of the abstraction as a measured number."),
    ("18", "transforms-filters",
     "Transforms and filters with a numerical acceptance test",
     "Nucleo alone",
     "Output that must match a double-precision host reference within a stated "
     "tolerance, with cycles per transform reported and the cache state named."),
    ("19", "caches-mpu-dma",
     "Caches, the MPU, and why DMA reads stale bytes",
     "Nucleo plus an ADXL345 breakout on a breadboard",
     "The corruption reproduced on purpose across a thousand runs, then removed "
     "three ways, with the naive variant failing at a measured rate."),
    ("20", "rtos-task-set",
     "Three jobs at once: the node as an RTOS application",
     "Nucleo plus the IKS4A1",
     "P08 to P12 rebuilt as a statically allocated task set, with a latency "
     "distribution over a million events against a deadline stated in advance."),
]

WRITTEN = {"02", "06", "09"}

TEMPLATE = """# P{nn}: {title}

**Not started.** This directory exists so the shape of the volume is visible
from the start rather than arriving a project at a time. Nothing here runs yet.

**What it is.** {about}

**What it needs on the bench.** {bench}

**What has to come first.** P01 owns the cross toolchain file, the startup code
and the linker script, so nothing in this repository can be flashed until that
project exists. As of Thursday 1 October 2026 win11 aquamarine has no
`arm-none-eabi-gcc`, no flashing tool and no `ninja`, so the target half of every
project here is blocked on a download rather than on a question.

Chapter {nn} of [the book][book] is the written design. When work starts here it
follows that chapter's acceptance criteria, and anything the chapter leaves as
"not measured" is either measured or still says so.

[book]: https://github.com/ambrosiobing/firmware-nucleo-h7
"""


def main() -> int:
    written, skipped = 0, 0
    for nn, slug, title, bench, about in SPEC:
        directory = PROJECTS / "P{}-{}".format(nn, slug)
        if not directory.is_dir():
            print("MISSING directory: {}".format(directory))
            return 1
        readme = directory / "README.md"
        if nn in WRITTEN or readme.exists():
            skipped += 1
            continue
        readme.write_text(
            TEMPLATE.format(nn=nn, title=title, bench=bench, about=about),
            encoding="utf-8",
            newline="\n",
        )
        print("wrote  {}".format(readme.relative_to(ROOT)))
        written += 1
    print("\n{} stubs written, {} left alone".format(written, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
