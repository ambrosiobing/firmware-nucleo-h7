# P01 in Python

**Nothing here yet.** MicroPython has an official board definition for this exact
part with its own linker script, and prebuilt firmware is published, so this
variant costs a drag and drop and no build at all.

What it can and cannot do. It blinks LD1 on PB0 and prints, which is first light.
It does not decode the clock tree from RCC, it does not calibrate a delay loop
against the cycle counter, and it cannot make any of this volume's timing claims.
That is the trade, and naming it is the point of the variant.


## What it is proven against

The board itself: LD1 green on PB0 blinking, the banner on COM13 at 115200, and
the clock the part reports about itself.