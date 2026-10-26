# P14: The IKS5A1, low and high g at once

Status: c=host cpp=none python=host rust=none

**Started on the host, Monday 26 October 2026.** Chapter 14's step 7 says to
write the record's decoder at the same time as the firmware, because "a format
with no second implementation is a format with no specification". That is this,
and it needed no shield: the header parse, the conversion of counts to
engineering units through the scale codes the record carries, the barometric
relation of step 9 and the drain time step 10 says to compute before looking for
it.

**What it is.** A shock and altitude logger that records a fine-resolution
acceleration window and a wide-range pressure trace around an event the fine
channel cannot see, because the coarse channel and the fine channel are both
running the whole time.

## What runs

| Piece | What proves it |
|---|---|
| `c/recdecode.c`, the parse, the conversions, the relation, the drain | 11 header cases, 6 conversions, 5 altitudes, 3 resolutions, 4 drains, 8 refusals |
| `python/recdecode.py`, the twin, standard library only | the same cases, and both of the chapter's printed forms kept unused |
| `vectors.json`, the oracle | integers compared exactly, altitudes to a relative 1e-12 and nothing tighter |

**What is exact and what is not, stated once.** Header fields and counts are
integers and compared exactly. The sensitivity table's entries are powers of two,
so every conversion is a multiply or divide that is exact in binary. Altitudes
raise a ratio to a fractional power, which is a library call in both languages
and need not agree in the last bit between gcc's and CPython's, so those are
compared to a relative 1e-12, which is nine orders of magnitude tighter than
anything this project claims about a pressure sensor.

**The sensitivity table is an argument and not a constant.** Neither sensor's
datasheet is on this bench, and chapter 14's own reason for putting scale codes
in the record applies one level down: a sensitivity compiled into a decoder is
exactly the constant step 9 exists to remove. So the table of code to sensitivity
is passed in, a code the table does not define is **refused rather than
decoded**, and every finding below is about the ratio between two entries rather
than about either value.

**The header is parsed and not cast.** The chapter declares a packed structure
for the firmware to write, which is right there. Casting a byte buffer onto it on
a host would assume the host's byte order and its tolerance for unaligned
members, and the whole value of a second implementation is that it shares no
assumption with the first.

## The event window records altitude at the worse resolution

| Barometer range | Counts per hPa | Altitude resolution | Selected by step 6 for |
|---|---|---|---|
| narrow | 4096 | 2.0326 mm per count | the quiet state |
| wide | 2048 | **4.0653 mm per count** | **inside the event window** |

Step 6 selects the narrow scale for the quiet state, "where the finer sensitivity
is what gives altitude any resolution", and the wide scale inside the event
window. Altitude resolution is the pressure resolution times the slope of step
9's relation, **8.3257 metres per hectopascal** at the reference, checked here
analytically and by difference quotient. So the resolution degrades by exactly
the ratio of the two sensitivities, during the event the recorder exists to
capture.

Whether that is the wrong trade depends on whether the event moves the pressure
past the narrow scale's end, which is a measurement nobody here has made. What is
wrong is leaving it unstated, so `rec_altitude_resolution_m` reports it and the
number travels with the trace.

## The altitude is differential and the record carries no reference

Step 9's relation needs `p0`. The record of step 7 carries `tick_event`, three
sample counts, both acceleration scale codes, the barometer range and the flags.
It does not carry a reference pressure, although the argument for keeping the
scales is that a record should be readable years later.

**One pressure of 1013.25 hPa reads as -83.7 m against a reference ten
hectopascals away, and as exactly 0 m against its own.** Ten hectopascals is
ordinary weather. The experiment in step 8 squeezes a sealed bag, where only the
difference matters, so the reference belongs in the record beside the scales for
the same reason the scales do. `rec_altitude_m` takes it as an argument and
refuses a non-positive one rather than carrying a default.

## A small correction to step 10

| | Drain time |
|---|---|
| step 10 as printed, 768 x 9 / 400000 | 17.280 ms |
| with the address and register bytes its own comment names | 17.325 ms, **+0.26 per cent** |

The comment says "128 samples of 6 bytes is 768 bytes, plus address and register
overhead" and the sum that follows contains no overhead. The arithmetic is right
and the difference changes no conclusion. It is recorded because the sentence and
the sum disagree and a reader cannot tell which was meant.

## What is not here

Both acceleration channels proved live, the watermark line, the circular
pre-trigger ring, the arming on the high-g line, the cost of changing the
barometer's full scale, the foam-pad event, the sealed bag, and the rate at which
the bus stops keeping up. **There is no measured number anywhere in this
project**, and the status line says host for that reason. The drain time above is
computed from a bus rate, which is what step 10 asks for and explicitly not a
measurement.

**What it needs on the bench.** The X-NUCLEO-IKS5A1 shield, which the inventory
does not hold.

Chapter 14 of [the book][book] is the written design.

[book]: ../../chapters/14-the-iks5a1.md
