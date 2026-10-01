# P15: Eight by eight time of flight, decided on the MCU

**Not started.** This directory exists so the shape of the volume is visible
from the start rather than arriving a project at a time. Nothing here runs yet.

**What it is.** Multizone ranging with the sensor firmware upload as an explicit bring-up step, and a zone policy with hysteresis.

**What it needs on the bench.** Nucleo plus the X-NUCLEO-53L8A1 shield

**What has to come first.** P01 owns the cross toolchain file, the startup code
and the linker script, so nothing in this repository can be flashed until that
project exists. As of Thursday 1 October 2026 win11 aquamarine has no
`arm-none-eabi-gcc`, no flashing tool and no `ninja`, so the target half of every
project here is blocked on a download rather than on a question.

Chapter 15 of [the book][book] is the written design. When work starts here it
follows that chapter's acceptance criteria, and anything the chapter leaves as
"not measured" is either measured or still says so.

[book]: ../../chapters/15-eight-by-eight-time-of-flight.md
