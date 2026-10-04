# P01 in Python

**Nothing here yet.** MicroPython has an official board definition for this exact
part with its own linker script, and prebuilt firmware is published, so this
variant costs a drag and drop and no build at all.

What it can and cannot do. It blinks LD1 on PB0 and prints, which is first light.
It does not decode the clock tree from RCC, it does not calibrate a delay loop
against the cycle counter, and it cannot make any of this volume's timing claims.
That is the trade, and naming it is the point of the variant.


## What it is proven against

**Two things, since Sunday 4 October 2026, and they are unusually far apart here.**

The host oracle is [`../clock_vectors.json`](../clock_vectors.json), and a Python
decode of the clock tree is a plain function over seven integers that can be held
to it row for row. That is the one part of P01 where the interpreter is at no
disadvantage whatever, because the arithmetic has no deadline and touches no
register. Note what it does NOT make true: this would be ordinary Python run on a
host, not MicroPython on the part.

The board itself is the second and the restricted one: LD1 green on PB0 blinking
and the banner on COM13 at 115200 under MicroPython. No clock tree decode from
RCC, no delay loop calibrated against the cycle counter, and none of this
volume's timing claims.