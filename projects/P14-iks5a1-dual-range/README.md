# P14: The IKS5A1: low and high g at once, wide pressure

**Not started.** This directory exists so the shape of the volume is visible
from the start rather than arriving a project at a time. Nothing here runs yet.

**What it is.** Both acceleration ranges in use simultaneously, which is what this shield is for, and a dual full-scale barometer.

**What it needs on the bench.** Nucleo plus the X-NUCLEO-IKS5A1 shield

**What has to come first.** P01 owns the cross toolchain file, the startup code
and the linker script, so nothing in this repository can be flashed until that
project exists. As of Thursday 1 October 2026 win11 aquamarine has no
`arm-none-eabi-gcc`, no flashing tool and no `ninja`, so the target half of every
project here is blocked on a download rather than on a question.

Chapter 14 of [the book][book] is the written design. When work starts here it
follows that chapter's acceptance criteria, and anything the chapter leaves as
"not measured" is either measured or still says so.

[book]: ../../chapters/14-the-iks5a1.md
