# The states and the events, written before the code

The chapter makes this a step rather than advice: write the states and the events
down, one sentence each, in a file committed before `node_sm.c` exists.

The rule that makes the sentences worth writing: **if a state cannot be described
in one sentence it is two states, and if an event has two meanings it is two
events.** Every entry below is one sentence, and that was a constraint on the
design rather than a style choice.

## The eight states

| State | What it means |
|---|---|
| `S_INIT` | Before the first tick, when nothing has been sensed and no sequence number has been used. |
| `S_IDLE` | Waiting for the next periodic wake, with no cycle in progress. |
| `S_SENSE` | A sample is being taken and the block has not arrived yet. |
| `S_FEATURE` | A block has arrived and is being reduced to one number. |
| `S_ENCODE` | The number is being packed into the payload P09 defines. |
| `S_TX` | The payload has been handed to the transmit stub and no answer has come back. |
| `S_BACKOFF` | A transmit attempt failed and the next one is waiting for the backoff to elapse. |
| `S_FAULT` | Something unrecoverable happened, and only a reset leaves this state. |

`S_FAULT` having no exit is a design decision and the test asserts it: every event
posted there must be unhandled. A fault state with a way out that nobody designed
is how a board comes back to life in an unknown condition.

## The nine events, and who may post each

| Event | What it means | Who posts it |
|---|---|---|
| `E_TICK` | The periodic wake fired. | P07's real-time clock wake |
| `E_BLOCK` | A block of samples is ready and `pending_sample` is set. | P06's acquisition |
| `E_FEATURE_DONE` | The feature has been computed from the block. | the main loop, after the reduction |
| `E_FRAME_READY` | The payload is ready to be packed. | the main loop |
| `E_TX_OK` | The transmit stub accepted the frame. | the transmit stub |
| `E_TX_FAIL` | The transmit stub refused the frame. | the transmit stub |
| `E_TIMEOUT` | A wait elapsed: either a sense that produced no block, or a backoff. | the main loop's timer |
| `E_BUTTON` | The user asked for a cycle now, out of turn. | the user button on PC13 |
| `E_FAULT` | Something unrecoverable happened. | any layer, once |

**`E_TIMEOUT` is the one entry worth arguing about.** It means two things, a sense
that produced nothing and a backoff that elapsed, which by the rule above should
make it two events. It stays one because the two meanings are disambiguated by the
state it arrives in and never overlap: `S_SENSE` and `S_BACKOFF` are the only
states with a timeout row, and no state has both. Splitting it would add an event
whose only purpose is to carry information the state already carries. That is a
judgement against the rule and it is recorded here rather than left as an
inconsistency somebody else has to notice.

## What is deliberately not in a row

A row is a from-state, an event, an optional guard, an action and a to-state.
Nothing else. The retry limit is a constant in the header, not a column, because a
row carrying it would describe policy and transition together and neither would be
readable on its own. Timeout durations are the caller's, for the same reason.

## What the table does not say

The table says what happens. It does not say how long anything takes, how often the
wake fires, or what the feature means. Those belong to P06, P07 and P18, and a
table that tried to carry them would be the whole node rather than its control
flow.
