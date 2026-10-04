# P01 in Python

**The host half is here and the board half is not**, which the status line says
as `python=host`. These are two different pieces of work and conflating them
would be the easiest way to overstate this directory.

`clocktree.py` is this project's clock tree decode, a pure function of seven
integers, held to [`../clock_vectors.json`](../clock_vectors.json) row for row.
This is the one part of P01 where the interpreter is at no disadvantage whatever:
no deadline, no register, no interrupt.

**Three places where Python had to be made to behave**, and each would have
passed a casual test while differing from the C:

- the overflow guard. C refuses when the PLL product would leave 32 bits; Python
  has unbounded integers and would return the larger number, so the guard is an
  explicit comparison here rather than a consequence of the arithmetic.
- the rounding. `-7 // 2` is -4 in Python and -3 in C, so a floor division in the
  parts-per-million arithmetic would disagree with the other three
  implementations on every negative row in the oracle.
- the saturation. A signed 32-bit bound means nothing to Python, so the clamp is
  written out.

Still to come: MicroPython on the part. There is an official board definition for
this exact die with its own linker script and prebuilt firmware, so it costs a
drag and drop and no build. It blinks LD1 on PB0 and prints, which is first
light. It does not decode the clock tree from RCC, it does not calibrate a delay
loop against the cycle counter, and it cannot make any of this volume's timing
claims. That is the trade, and naming it is the point of the variant.


## What it is proven against

**Two things, since Sunday 4 October 2026, and they are unusually far apart here.**

The host oracle is [`../clock_vectors.json`](../clock_vectors.json), and a Python
decode of the clock tree is a plain function over seven integers that can be held
to it row for row. That is the one part of P01 where the interpreter is at no
disadvantage whatever, because the arithmetic has no deadline and touches no
register. Note what it does NOT make true: this would be ordinary Python run on a
host, not MicroPython on the part.

The board itself is the second and the restricted one, and is not reached yet:
LD1 green on PB0 blinking and the banner on COM13 at 115200 under MicroPython. No
clock tree decode from RCC, no delay loop calibrated against the cycle counter,
and none of this volume's timing claims.