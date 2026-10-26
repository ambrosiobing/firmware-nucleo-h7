# P11: An AT engine that never blocks

Status: c=host cpp=none python=host rust=none

**Started on the host, Monday 26 October 2026, with the three pieces of chapter 11
that need no module.** The engine itself needs a modem on a serial line. Its line
classifier, its deadline test and its backoff do not, and one of the three cannot
be tested on hardware at all.

**What it is.** A command queue with timeouts, backoff and unsolicited results,
where no call blocks and the completion callback fires exactly once in every
outcome.

## The piece that a board could not prove

**Chapter 11's step 4 writes the deadline comparison as a signed difference** and
gives one line of reason: the obvious form "is correct for forty-nine days and
then is not". **A board cannot demonstrate that without waiting forty-nine days.**
A host sets the millisecond counter to whatever it likes.

Five of the nine deadline cases sit at or across the wrap, and two of them pin the
exact millisecond where the technique's own bound gives out: a deadline more than
half the wrap period away is reported as already passed. **That is a property of
the method rather than a bug in it**, and the header states the bound because the
usual summary, "correct across the wrap", is true only inside it.

A property test walks two thousand milliseconds across the boundary and requires
the answer to flip exactly once, at the deadline, with no second flip from the
wrap. A falsifier counts how many oracle cases the naive `now >= deadline` gets
wrong, so the wrap cases cannot quietly become decoration.

## What runs

| Piece | What proves it |
|---|---|
| `c/atparse.c`, the classifier, the deadline test, the backoff | 36 oracle cases, four property tests, and `-Wconversion -Wsign-conversion -Wpedantic`, which CI passes over it |
| `python/atparse.py`, the twin | the same cases; the 32 bit wrap is written out because Python has no 32 bit integers |
| `vectors.json`, the oracle | checked against a modular distance and against the formula longhand, which **found an error in it** |

## Two things reading the chapter closely turned up

**The cap bounds the doubling and not the result.** Step 8's scatter is applied
after the cap, so the returned delay runs from seven eighths of the interval to
one and an eighth of it, and **can exceed `cap_ms` by an eighth**. The oracle pins
a case where it does, at 35 999 ms against a cap of 32 000, and a property test
walks all 8 000 draws and asserts the band is contiguous and does pass the cap. It
is written as the chapter writes it; what is new here is that the consequence is
recorded rather than discovered later.

**A cap above half the range overflows the doubling.** With `cap_ms` near the top
of the type the shift can wrap and the function would return a plausible small
delay instead of a large one. That case is refused rather than returned.

## One deliberate difference from the sketch

**The random draw is a parameter here and a function call in the sketch.** Reading
a global source inside the function makes the result unprovable: a test can assert
a band and never a value. As a parameter the function is pure, the oracle states
exact values, and the engine passes whatever source it has. The reason is in the
header beside the signature.

## What is not here

The engine's queue, the completion callback that must fire exactly once in every
outcome, the unsolicited handler table, the hundred injected faults of step 9 and
the no-SIM stop of step 10. Those need the module, the circular receive path of
chapter 4 and the framing of chapter 5. **The prefix matching also accepts `OKAY`
as a success**, which is a real consequence of matching prefixes rather than whole
lines; the oracle pins it rather than hiding it, and whether a module ever emits
such a line is a question for its manual.

**What it needs on the bench.** Nucleo plus a cellular module on its own supply,
which the inventory does not currently hold.

Chapter 11 of [the book][book] is the written design.

[book]: ../../chapters/11-an-at-engine-that-never-blocks.md
