# P15: Eight by eight time of flight, decided on the MCU

Status: c=host cpp=none python=host rust=none

**Started on the host, Monday 26 October 2026, and scoped to what the chapter
actually specifies.** The sensor reports sixty-four zones several times a second
and the node sends the host one status byte. The part that makes that byte worth
anything is the hysteresis, which is what stops the output changing state every
time a person shifts their weight, and a state machine over a sequence of
distances needs no sensor, no bus and no board to prove.

**What it is.** A ranging node that uploads an 84 kilobyte firmware image to its
sensor at every power-on and reduces sixty-four zones to one status byte.

## What runs

| Piece | What proves it |
|---|---|
| `c/zonetrack.c`, the four states and their six edges | 19 oracle cases, a 600 frame hysteresis property with its own falsifier, and `-Wconversion -Wsign-conversion -Wpedantic`, which CI passes over it |
| `python/zonetrack.py`, the twin | the same 19 cases, and a pairwise comparison with the C |
| `vectors.json`, the oracle | checked against the figure's edge set before either implementation was allowed to agree with it |

## Where the specification came from, and the two sources disagree in coverage

**Chapter 15's step 9 gives two thresholds and the confirmation count**: 800 mm to
enter, 950 mm to leave, three agreeing frames out of five. **Figure 15.4 gives
those and a third threshold of 1500 mm** that the step 9 sketch omits, as the gate
between clear and approaching and between leaving and clear. The figure is the
more complete of the two and this project follows it:

| From | To | Condition |
|---|---|---|
| clear | approaching | under 1500 mm, immediately |
| approaching | present | under 800 mm, three frames of five |
| present | leaving | over 950 mm, three frames of five |
| leaving | clear | over 1500 mm, immediately |

**The thresholds are parameters and not constants.** The figure's own note says
the numbers are "a starting point measured against a real target rather than
constants to be trusted", so compiling them in would make this code disagree with
the chapter the first time a measurement moved one.

## Three decisions this project made that the chapter does not

Each is in the code with its reason beside it, because a reader finding them later
should see a decision rather than an accident.

**The window is cleared on every state change.** Votes gathered while approaching
say nothing about whether to leave, and carrying them over would let a target that
crossed 800 mm three times on the way in satisfy the exit test on the way out.

**Approaching returns to clear if the target leaves range without ever
confirming.** The figure draws no edge for it, and a node left in approaching for
ever after somebody walks out of the room would be worse than the alternative.

**Leaving returns to present if the target comes back.** The figure draws no edge
for that either, and waiting for the target to pass 1500 mm first before it could
be present again would report the room empty while somebody stood in it.

## What is deliberately not here

**The validity filter of step 8 needs a sigma limit, a minimum distance and the
driver's set of trusted status codes. The reduction needs an occupancy bucket
mapping and the status byte needs a confidence field. The chapter defines none of
those five.** Inventing them to fill a struct would be fiction of exactly the kind
this volume exists to avoid, and the chapter itself calls them measurements rather
than constants. They arrive when the sensor does.

So does everything else that needs hardware: the 84 kilobyte firmware upload and
its cost, the watchdog window that must be chosen after the reduction time is
known, the flash placement proof, and the hour-long run against a moving target
that turns the policy from a guess into a measurement by counting state changes.

**What it needs on the bench.** Nucleo plus the X-NUCLEO-53L8A1, which is the
shield already stacked on the board for other reasons.

Chapter 15 of [the book][book] is the written design. When the board half starts
it follows that chapter's acceptance criteria, and anything the chapter leaves as
"not measured" is either measured or still says so.

[book]: ../../chapters/15-eight-by-eight-time-of-flight.md
