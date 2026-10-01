# P13: The IKS4A1: FIFO, watermark, interrupt

**Not started.** This directory exists so the shape of the volume is visible
from the start rather than arriving a project at a time. Nothing here runs yet.

**What it is.** A logger that never polls, driven by the sensor's own watermark interrupt, and the shield's three bus topologies.

**What it needs on the bench.** Nucleo plus the X-NUCLEO-IKS4A1 shield

**What has to come first.** P01 owns the cross toolchain file, the startup code
and the linker script, so nothing in this repository can be flashed until that
project exists. As of Thursday 1 October 2026 win11 aquamarine has no
`arm-none-eabi-gcc`, no flashing tool and no `ninja`, so the target half of every
project here is blocked on a download rather than on a question.

Chapter 13 of [the book][book] is the written design. When work starts here it
follows that chapter's acceptance criteria, and anything the chapter leaves as
"not measured" is either measured or still says so.

[book]: ../../chapters/13-the-iks4a1.md
