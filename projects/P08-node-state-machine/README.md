# P08: the node's state machine, with a transmit that is a stub

Status: c=host cpp=host python=host rust=host

Eight states, nine events, eighteen rows, and a dispatcher that cannot block.

**Four implementations, all four proven on the host Saturday 3 October 2026**,
first in WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then in CI
at commit 22ec8ad. They agree on every row index taken across twenty event
sequences and 2674 events. Each calls its own language's P09 encoder rather than
a copy, so the frame the stub hands over is four independent confirmations of
P09's bit layout.

**The comparison is stricter here than in P09 or P05, and it has to be.** Those
two have an external fact to check against: six hand-computed vectors, and a
published check value. P08 has none, because the table **is** the specification.
So `python/tests/test_node_sm_parity.py` compares the row index each
implementation takes for every event, not merely the state it ends in, which
pins all four tables to one order. That matters because the two `TX_FAIL` rows in
`S_TX` are told apart only by their guards: put the out-of-retries row first and
the retry row becomes unreachable, the node gives up after one attempt instead of
three, and a comparison of outcomes alone could miss it.

**State: written and proven on the host.** This is the only piece of application
logic in the volume that can be proven completely without the board, because it
touches no peripheral, allocates nothing and never blocks. **8 checks pass, all 18
transition rows reachable, 0 unhandled events.**

## Both acceptance criteria are met

**Every row of the table is proven reachable by a test.** Not "the states are
covered" and not "the happy path works": every row, with its hit count, and the
test fails naming any row never taken. A row nobody can reach is either dead
behaviour or a design that cannot do what the table says.

    all 18 rows reachable; unhandled events posted: 0

**The stub payload matches P09's encoder byte for byte.** This is P09's acceptance
criterion as much as P08's, and the only place in this volume where two projects
verify each other. It holds by construction, because the encode action calls
`payload_encode` rather than reimplementing the packing, and the test exists so it
keeps holding: a later decision to reimplement it would fail here rather than
quietly producing a second layout. Checked over eight samples including both
extremes of the eighteen-bit signed field.

## The design, and the one restriction that is the design

A row is a from-state, an event, an optional guard, an action and a to-state.
Nothing else. **The retry limit is a constant in the header, not a column**, because
a row carrying it would describe policy and transition together and neither would be
readable on its own.

The dispatcher is a dozen lines and the table is the behaviour, so a reader who
wants to know what the node does reads the table, and a reviewer can check it
against [STATES.md](STATES.md) row by row. That file was written before the code,
which the chapter makes a step rather than advice.

Actions are pure functions of the context. None blocks, none fails silently, and
**none posts an event**: an action that posted its own event would make the table a
graph with hidden edges. Guards answer one question and change nothing.

`S_FAULT` has no outgoing row, and the test asserts that: every one of the nine
events posted there must be unhandled. A fault state with an exit nobody designed is
how a board comes back to life in an unknown condition.

An event that does not apply in the current state is ignored on purpose and
**counted**. Counting is what turns "ignored" into something a reader can check: a
rising count in a real run means something is posting events the design did not
expect.

## A defect the coverage test found

`tx_attempts` was incremented on `E_TX_FAIL`, so the counter meant "failures so
far" while the guard read it as "attempts so far", and the retry limit was off by
one: **four attempts were made where three were intended.** The attempt is now
counted where the attempt happens, once when the frame is first handed to the stub
and once more on each retry out of backoff.

This is the kind of defect a happy-path test never sees and a reviewer reading the
table would not see either, because the table was right and the counter was wrong.
The test that found it is the one that drives the limit to its end and asserts the
state afterwards.

## Two bookkeeping rules the tests pin

**The sequence wraps at 512**, because P09's field is nine bits. A sequence that
walked past 511 would encode as something else and the two projects would disagree
about the payload while both looked correct.

**An abandoned cycle still consumes its sequence number.** Otherwise a receiver
cannot tell a lost frame from a repeated one, which is a worse failure than the lost
frame itself.

## No prior art

The chapter found none worth the name for this, so every line is the author's. That
is unusual in this volume and it is stated rather than implied: there was nothing to
copy and therefore nothing to copy wrong. It also means the design decisions above
are arguable rather than inherited, which is why each one says why.

## Layout

    c/node_sm.{c,h}          the table and the dispatcher
    projects/P08-.../STATES.md   the states and events, written before the code
    python/tests/test_node_sm.py eight checks, no hardware

## Not done

- No target build. The state machine needs nothing from the board, but the events
  do: `E_TICK` is P07's wake, `E_BLOCK` is P06's acquisition, and `E_BUTTON` is the
  user button, so the node only runs as a node once those exist.
- The transmit step is a stub and stays one. There is no radio on this bench, and
  the stub records the attempt without touching the frame, which the test checks:
  a stub that rewrote or padded what it was given would make this project look
  finished and leave P09's codec untested from this side.
- The feature is the sample unchanged. P18 is where a real transform lives. Keeping
  it trivial is also what lets the test predict the frame exactly, which is what
  makes the cross-check with P09 mean anything.
- `E_TIMEOUT` carries two meanings, disambiguated by the state it arrives in. That
  is a judgement against this project's own rule about events, and it is recorded
  in STATES.md rather than left for somebody else to notice.
