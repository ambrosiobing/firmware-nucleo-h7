#!/usr/bin/env python3
"""P08's state machine in Python: the same eighteen rows, in the same order.

The table is the behaviour, here as in the C. A reader who wants to know what the
node does reads `TABLE` below and compares it row by row with `../STATES.md` and
with `../c/node_sm.c`. The dispatcher is nine lines.

**Why the row order matters and is part of the comparison.** Two rows share a
from-state and an event, the pair of `E_TX_FAIL` rows in `S_TX`, and they are
distinguished only by their guards. The retry row comes first. If the
out-of-retries row came first the retry row would be unreachable, and no
behavioural test over whole cycles would necessarily notice. So
`python/tests/test_node_sm_parity.py` compares the **row index** each
implementation takes for each event, not merely the state it lands in. That pins
the four tables to one order.

**The encoder is P09's, not a copy.** `act_encode` calls `firmkit.payload.encode`,
which is the module P09's own tests drive and the file copied to the board under
MicroPython. The acceptance criterion P08 shares with P09 is that the stub's bytes
equal what P09's encoder produces for the same input, and calling it rather than
reimplementing it makes that true by construction. The C does the same thing with
`payload_encode`, the C++ with `payload.hpp` and the Rust with the `p09-payload`
crate.

Nothing here allocates during a dispatch, nothing blocks, and nothing posts an
event from inside an action: the dispatcher returns and the caller posts what
comes next. An action that posted its own event would make the table a graph with
hidden edges.
"""
from __future__ import annotations

from dataclasses import dataclass, field

try:
    from firmkit import payload
except ImportError:                     # the flat form, as on the board
    import payload                      # type: ignore

# The eight states and the nine events, as strings rather than an enum. The wire
# protocol the parity filter speaks uses these names, and the C's
# node_sm_state_name returns exactly these, so one spelling serves both.
STATES = ("INIT", "IDLE", "SENSE", "FEATURE", "ENCODE", "TX", "BACKOFF", "FAULT")
EVENTS = (
    "TICK", "BLOCK", "FEATURE_DONE", "FRAME_READY",
    "TX_OK", "TX_FAIL", "TIMEOUT", "BUTTON", "FAULT",
)

# The retry limit, here rather than in a row, because a row carrying it would
# describe policy and transition together and neither would be readable.
TX_ATTEMPT_LIMIT = 3


@dataclass
class Ctx:
    """Everything the machine knows. Flat and fixed, as the C struct is, so a test
    can build one and compare two by value."""

    state: str = "INIT"
    tx_attempts: int = 0
    cycles_ok: int = 0
    cycles_dropped: int = 0
    feature: int = 0
    sequence: int = 0
    pending_sample: int = 0        # the seam: a caller sets this before BLOCK
    frame: bytes = b""
    unhandled: int = 0


# ------------------------------------------------------------------- actions

def act_nothing(ctx: Ctx) -> None:
    pass


def act_start_cycle(ctx: Ctx) -> None:
    ctx.tx_attempts = 0
    ctx.frame = b""


def act_take_feature(ctx: Ctx) -> None:
    # One line, because this project's subject is the table rather than the
    # signal processing: P18 is where a real transform lives. Keeping it trivial
    # also keeps a test able to predict the frame exactly.
    ctx.feature = ctx.pending_sample


def act_encode(ctx: Ctx) -> None:
    ctx.frame = payload.encode(
        {
            "version": 1,
            "flags": 0,
            "sequence": ctx.sequence,
            "feature": ctx.feature,
            "battery": 40,
        }
    )


def act_encode_and_attempt(ctx: Ctx) -> None:
    # tx_attempts counts ATTEMPTS, not failures. The C got this wrong first, by
    # incrementing on TX_FAIL, which made the counter mean "failures so far"
    # while the guard read it as "attempts so far" and the limit was off by one.
    # So the attempt is counted where the attempt happens.
    act_encode(ctx)
    ctx.tx_attempts = 1


def act_another_attempt(ctx: Ctx) -> None:
    ctx.tx_attempts += 1


def act_cycle_ok(ctx: Ctx) -> None:
    ctx.cycles_ok += 1
    ctx.sequence = (ctx.sequence + 1) & 0x1FF      # wraps at 512, as P09's field does


def act_cycle_dropped(ctx: Ctx) -> None:
    # Counted, not hidden. A node that silently abandons cycles looks exactly
    # like one that is working.
    ctx.cycles_dropped += 1
    ctx.sequence = (ctx.sequence + 1) & 0x1FF


# -------------------------------------------------------------------- guards

def guard_always(ctx: Ctx) -> bool:
    return True


def guard_may_retry(ctx: Ctx) -> bool:
    return ctx.tx_attempts < TX_ATTEMPT_LIMIT


def guard_out_of_retries(ctx: Ctx) -> bool:
    return ctx.tx_attempts >= TX_ATTEMPT_LIMIT


# --------------------------------------------------------------------- table

# from, event, guard, action, to, name. The order is the C's order, and the
# parity test holds it to that.
TABLE = (
    ("INIT",    "TICK",         guard_always,         act_start_cycle,        "SENSE",   "init: first tick starts a cycle"),
    ("INIT",    "FAULT",        guard_always,         act_nothing,            "FAULT",   "init: fault before anything"),

    ("IDLE",    "TICK",         guard_always,         act_start_cycle,        "SENSE",   "idle: periodic wake"),
    ("IDLE",    "BUTTON",       guard_always,         act_start_cycle,        "SENSE",   "idle: user forced a cycle"),
    ("IDLE",    "FAULT",        guard_always,         act_nothing,            "FAULT",   "idle: fault"),

    ("SENSE",   "BLOCK",        guard_always,         act_nothing,            "FEATURE", "sense: a block arrived"),
    ("SENSE",   "TIMEOUT",      guard_always,         act_cycle_dropped,      "IDLE",    "sense: no block in time"),
    ("SENSE",   "FAULT",        guard_always,         act_nothing,            "FAULT",   "sense: fault"),

    ("FEATURE", "FEATURE_DONE", guard_always,         act_take_feature,       "ENCODE",  "feature: computed"),
    ("FEATURE", "FAULT",        guard_always,         act_nothing,            "FAULT",   "feature: fault"),

    ("ENCODE",  "FRAME_READY",  guard_always,         act_encode_and_attempt, "TX",      "encode: payload packed, first attempt"),
    ("ENCODE",  "FAULT",        guard_always,         act_nothing,            "FAULT",   "encode: fault"),

    ("TX",      "TX_OK",        guard_always,         act_cycle_ok,           "IDLE",    "tx: accepted"),
    ("TX",      "TX_FAIL",      guard_may_retry,      act_nothing,            "BACKOFF", "tx: failed, will retry"),
    ("TX",      "TX_FAIL",      guard_out_of_retries, act_cycle_dropped,      "IDLE",    "tx: failed, out of retries"),
    ("TX",      "FAULT",        guard_always,         act_nothing,            "FAULT",   "tx: fault"),

    ("BACKOFF", "TIMEOUT",      guard_always,         act_another_attempt,    "TX",      "backoff: elapsed, another attempt"),
    ("BACKOFF", "FAULT",        guard_always,         act_nothing,            "FAULT",   "backoff: fault"),

    # FAULT has no outgoing row on purpose. Only a reset leaves it, and every
    # event posted there must be unhandled rather than quietly recovering. A
    # fault state with a way out that nobody designed is how a board comes back
    # to life in an unknown condition.
)

ROW_COUNT = len(TABLE)


# ---------------------------------------------------------------- dispatcher

def init() -> Ctx:
    return Ctx()


def dispatch(ctx: Ctx, event: str) -> int:
    """Post one event. Returns the index of the row taken, or -1 when no row
    matches, which is not an error: an event that does not apply in the current
    state is ignored on purpose, and the count of those is worth watching."""
    if event not in EVENTS:
        raise ValueError("unknown event: " + event)
    for i, (frm, ev, guard, action, to, _name) in enumerate(TABLE):
        if frm != ctx.state or ev != event:
            continue
        if not guard(ctx):
            continue
        # Action before the state change, so an action can read the state it is
        # leaving. Nothing depends on that today, and fixing the order now costs
        # nothing while discovering it later costs an evening.
        action(ctx)
        ctx.state = to
        return i
    ctx.unhandled += 1
    return -1


def row_name(row: int) -> str:
    return TABLE[row][5] if 0 <= row < ROW_COUNT else "out of range"
