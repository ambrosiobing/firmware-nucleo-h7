//! P08's node state machine in Rust: the same eighteen rows, in the same order.
//!
//! `no_std`, no allocation, nothing that blocks, and no action that posts an
//! event: `dispatch` returns and the caller posts what comes next. An action
//! that posted its own event would make the table a graph with hidden edges.
//!
//! **The row order is part of the comparison.** Two rows share a from-state and
//! an event, the pair of `TxFail` rows in `Tx`, told apart only by their
//! guards, and the retry row comes first. If it came second it would be
//! unreachable, and a test that only checked the resulting state might never
//! notice. So `python/tests/test_node_sm_parity.py` compares the **row index**
//! each implementation takes for each event, which pins all four tables to one
//! order.
//!
//! **The encoder is P09's crate, not a copy.** `p09-payload` is a path
//! dependency, which is the first inter-crate edge in this workspace and is
//! deliberate: the acceptance criterion P08 shares with P09 is that the stub's
//! bytes equal what P09's encoder produces, and depending on it makes that true
//! by construction rather than by agreement.
//!
//! **What Rust buys here, honestly.** Less than in P05, and the reason is worth
//! printing. The C's table is already data, so there is no memory hazard to
//! remove and no undefined behaviour to avoid. What the type system does add is
//! that `State` and `Event` cannot be confused with each other or with an
//! integer, which two plain C enums can be, and that a row's action cannot
//! return a value the dispatcher then ignores. The cost is that the table is
//! function pointers in a `const` array rather than a brace-initialised
//! aggregate, which reads very nearly the same.

#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]

use p09_payload::{encode, Payload, PAYLOAD_BYTES};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    Init,
    Idle,
    Sense,
    Feature,
    Encode,
    Tx,
    Backoff,
    Fault,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Event {
    Tick,
    Block,
    FeatureDone,
    FrameReady,
    TxOk,
    TxFail,
    Timeout,
    Button,
    Fault,
}

/// The retry limit, here rather than in a row, because a row carrying it would
/// describe policy and transition together and neither would be readable.
pub const TX_ATTEMPT_LIMIT: u32 = 3;

/// Everything the machine knows. Flat and fixed, as the C struct is, so a test
/// can build one and compare two by value.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Ctx {
    pub state: State,
    pub tx_attempts: u32,
    pub cycles_ok: u32,
    pub cycles_dropped: u32,
    pub feature: i32,
    pub sequence: u32,
    /// The seam: a caller sets this before `Block`.
    pub pending_sample: i32,
    pub frame: [u8; 64],
    pub frame_len: u8,
    pub unhandled: u32,
}

impl Default for Ctx {
    fn default() -> Self {
        Self {
            state: State::Init,
            tx_attempts: 0,
            cycles_ok: 0,
            cycles_dropped: 0,
            feature: 0,
            sequence: 0,
            pending_sample: 0,
            frame: [0; 64],
            frame_len: 0,
            unhandled: 0,
        }
    }
}

// ------------------------------------------------------------------ actions

fn act_nothing(_ctx: &mut Ctx) {}

fn act_start_cycle(ctx: &mut Ctx) {
    ctx.tx_attempts = 0;
    ctx.frame_len = 0;
}

fn act_take_feature(ctx: &mut Ctx) {
    // One line: this project's subject is the table, not the signal
    // processing. P18 is where a real transform lives.
    ctx.feature = ctx.pending_sample;
}

fn act_encode(ctx: &mut Ctx) {
    let p = Payload {
        version: 1,
        flags: 0,
        sequence: ctx.sequence,
        feature: ctx.feature,
        battery: 40,
    };
    let mut out = [0u8; PAYLOAD_BYTES];
    // The encoder refuses only when the buffer is too small, and this one is
    // exactly PAYLOAD_BYTES, so the None arm is unreachable. It is written as a
    // match rather than an unwrap so that a future change to the buffer size
    // leaves a frame_len of zero rather than a panic in a no_std image.
    match encode(&mut out, &p) {
        Some(n) => {
            ctx.frame[..n].copy_from_slice(&out[..n]);
            ctx.frame_len = n as u8;
        }
        None => ctx.frame_len = 0,
    }
}

fn act_encode_and_attempt(ctx: &mut Ctx) {
    // tx_attempts counts ATTEMPTS, not failures. The C got this wrong first by
    // incrementing on TxFail, which made the counter mean "failures so far"
    // while the guard read it as "attempts so far", and the limit was off by
    // one. The attempt is counted where the attempt happens.
    act_encode(ctx);
    ctx.tx_attempts = 1;
}

fn act_another_attempt(ctx: &mut Ctx) {
    ctx.tx_attempts += 1;
}

fn act_cycle_ok(ctx: &mut Ctx) {
    ctx.cycles_ok += 1;
    ctx.sequence = (ctx.sequence + 1) & 0x1FF; // wraps at 512, as P09's field does
}

fn act_cycle_dropped(ctx: &mut Ctx) {
    // Counted, not hidden. A node that silently abandons cycles looks exactly
    // like one that is working.
    ctx.cycles_dropped += 1;
    ctx.sequence = (ctx.sequence + 1) & 0x1FF;
}

// ------------------------------------------------------------------- guards

fn guard_always(_ctx: &Ctx) -> bool {
    true
}

fn guard_may_retry(ctx: &Ctx) -> bool {
    ctx.tx_attempts < TX_ATTEMPT_LIMIT
}

fn guard_out_of_retries(ctx: &Ctx) -> bool {
    ctx.tx_attempts >= TX_ATTEMPT_LIMIT
}

// -------------------------------------------------------------------- table

pub struct Row {
    pub from: State,
    pub event: Event,
    pub guard: fn(&Ctx) -> bool,
    pub action: fn(&mut Ctx),
    pub to: State,
    pub name: &'static str,
}

pub static TABLE: [Row; 18] = [
    Row {
        from: State::Init,
        event: Event::Tick,
        guard: guard_always,
        action: act_start_cycle,
        to: State::Sense,
        name: "init: first tick starts a cycle",
    },
    Row {
        from: State::Init,
        event: Event::Fault,
        guard: guard_always,
        action: act_nothing,
        to: State::Fault,
        name: "init: fault before anything",
    },
    Row {
        from: State::Idle,
        event: Event::Tick,
        guard: guard_always,
        action: act_start_cycle,
        to: State::Sense,
        name: "idle: periodic wake",
    },
    Row {
        from: State::Idle,
        event: Event::Button,
        guard: guard_always,
        action: act_start_cycle,
        to: State::Sense,
        name: "idle: user forced a cycle",
    },
    Row {
        from: State::Idle,
        event: Event::Fault,
        guard: guard_always,
        action: act_nothing,
        to: State::Fault,
        name: "idle: fault",
    },
    Row {
        from: State::Sense,
        event: Event::Block,
        guard: guard_always,
        action: act_nothing,
        to: State::Feature,
        name: "sense: a block arrived",
    },
    Row {
        from: State::Sense,
        event: Event::Timeout,
        guard: guard_always,
        action: act_cycle_dropped,
        to: State::Idle,
        name: "sense: no block in time",
    },
    Row {
        from: State::Sense,
        event: Event::Fault,
        guard: guard_always,
        action: act_nothing,
        to: State::Fault,
        name: "sense: fault",
    },
    Row {
        from: State::Feature,
        event: Event::FeatureDone,
        guard: guard_always,
        action: act_take_feature,
        to: State::Encode,
        name: "feature: computed",
    },
    Row {
        from: State::Feature,
        event: Event::Fault,
        guard: guard_always,
        action: act_nothing,
        to: State::Fault,
        name: "feature: fault",
    },
    Row {
        from: State::Encode,
        event: Event::FrameReady,
        guard: guard_always,
        action: act_encode_and_attempt,
        to: State::Tx,
        name: "encode: payload packed, first attempt",
    },
    Row {
        from: State::Encode,
        event: Event::Fault,
        guard: guard_always,
        action: act_nothing,
        to: State::Fault,
        name: "encode: fault",
    },
    Row {
        from: State::Tx,
        event: Event::TxOk,
        guard: guard_always,
        action: act_cycle_ok,
        to: State::Idle,
        name: "tx: accepted",
    },
    Row {
        from: State::Tx,
        event: Event::TxFail,
        guard: guard_may_retry,
        action: act_nothing,
        to: State::Backoff,
        name: "tx: failed, will retry",
    },
    Row {
        from: State::Tx,
        event: Event::TxFail,
        guard: guard_out_of_retries,
        action: act_cycle_dropped,
        to: State::Idle,
        name: "tx: failed, out of retries",
    },
    Row {
        from: State::Tx,
        event: Event::Fault,
        guard: guard_always,
        action: act_nothing,
        to: State::Fault,
        name: "tx: fault",
    },
    Row {
        from: State::Backoff,
        event: Event::Timeout,
        guard: guard_always,
        action: act_another_attempt,
        to: State::Tx,
        name: "backoff: elapsed, another attempt",
    },
    Row {
        from: State::Backoff,
        event: Event::Fault,
        guard: guard_always,
        action: act_nothing,
        to: State::Fault,
        name: "backoff: fault",
    },
    // Fault has no outgoing row on purpose. Only a reset leaves it, and every
    // event posted there must be unhandled rather than quietly recovering. A
    // fault state with a way out that nobody designed is how a board comes back
    // to life in an unknown condition.
];

// --------------------------------------------------------------- dispatcher

/// Post one event. Returns the index of the row taken, or `None` when no row
/// matches, which is not an error: an event that does not apply in the current
/// state is ignored on purpose, and the count of those is worth watching.
pub fn dispatch(ctx: &mut Ctx, event: Event) -> Option<usize> {
    for (i, row) in TABLE.iter().enumerate() {
        if row.from != ctx.state || row.event != event {
            continue;
        }
        if !(row.guard)(ctx) {
            continue;
        }
        // Action before the state change, so an action can read the state it is
        // leaving.
        (row.action)(ctx);
        ctx.state = row.to;
        return Some(i);
    }
    ctx.unhandled += 1;
    None
}

/// The names the parity protocol uses, which are the C's spellings so that one
/// vocabulary serves all four implementations.
pub fn state_name(s: State) -> &'static str {
    match s {
        State::Init => "INIT",
        State::Idle => "IDLE",
        State::Sense => "SENSE",
        State::Feature => "FEATURE",
        State::Encode => "ENCODE",
        State::Tx => "TX",
        State::Backoff => "BACKOFF",
        State::Fault => "FAULT",
    }
}

pub fn event_from_name(text: &str) -> Option<Event> {
    Some(match text {
        "TICK" => Event::Tick,
        "BLOCK" => Event::Block,
        "FEATURE_DONE" => Event::FeatureDone,
        "FRAME_READY" => Event::FrameReady,
        "TX_OK" => Event::TxOk,
        "TX_FAIL" => Event::TxFail,
        "TIMEOUT" => Event::Timeout,
        "BUTTON" => Event::Button,
        "FAULT" => Event::Fault,
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The event sequences are deliberately NOT here. They live in
    /// `python/tests/test_node_sm_parity.py`, which drives all four
    /// implementations with one list. What belongs here is the properties that
    /// need no other implementation to be meaningful.
    #[test]
    fn every_row_is_reachable() {
        // This project's own acceptance criterion, in its own language: drive
        // sequences until no row has zero hits, and report which if any does.
        let mut hits = [0u32; 18];
        let mut drive = |events: &[Event], sample: i32| {
            let mut ctx = Ctx {
                pending_sample: sample,
                ..Default::default()
            };
            for &e in events {
                if let Some(row) = dispatch(&mut ctx, e) {
                    hits[row] += 1;
                }
            }
        };
        use Event::*;
        drive(&[Tick, Block, FeatureDone, FrameReady, TxOk], 5);
        drive(&[Tick, Timeout], 0);
        drive(&[Fault], 0);
        drive(&[Tick, Fault], 0);
        drive(&[Tick, Block, Fault], 0);
        drive(&[Tick, Block, FeatureDone, Fault], 0);
        drive(&[Tick, Block, FeatureDone, FrameReady, Fault], 0);
        drive(&[Tick, Block, FeatureDone, FrameReady, TxFail, Fault], 0);
        drive(&[Tick, Block, FeatureDone, FrameReady, TxOk, Button], 0);
        drive(&[Tick, Block, FeatureDone, FrameReady, TxOk, Fault], 0);
        // Three attempts then out of retries.
        drive(
            &[
                Tick,
                Block,
                FeatureDone,
                FrameReady,
                TxFail,
                Timeout,
                TxFail,
                Timeout,
                TxFail,
            ],
            7,
        );

        // Vec is fine here: this module is only compiled under cargo test,
        // where std is available. The library itself is no_std.
        let missed: Vec<&str> = hits
            .iter()
            .enumerate()
            .filter(|(_, &h)| h == 0)
            .map(|(i, _)| TABLE[i].name)
            .collect();
        assert!(missed.is_empty(), "rows never taken: {:?}", missed);
    }

    /// The limit is three attempts, not four. This is the off-by-one the C had
    /// when its counter meant failures instead of attempts.
    #[test]
    fn the_retry_limit_allows_exactly_three_attempts() {
        let mut ctx = Ctx {
            pending_sample: 1,
            ..Default::default()
        };
        for e in [
            Event::Tick,
            Event::Block,
            Event::FeatureDone,
            Event::FrameReady,
        ] {
            dispatch(&mut ctx, e);
        }
        assert_eq!(
            ctx.tx_attempts, 1,
            "the handover counts as the first attempt"
        );
        for expected in [2u32, 3] {
            dispatch(&mut ctx, Event::TxFail);
            assert_eq!(ctx.state, State::Backoff);
            dispatch(&mut ctx, Event::Timeout);
            assert_eq!(ctx.tx_attempts, expected);
        }
        // The third failure is out of retries: no backoff, the cycle is dropped.
        dispatch(&mut ctx, Event::TxFail);
        assert_eq!(ctx.state, State::Idle);
        assert_eq!(ctx.cycles_dropped, 1);
        assert_eq!(ctx.cycles_ok, 0);
    }

    /// Fault has no exit, and the test asserts it rather than trusting the
    /// table to stay that way.
    #[test]
    fn nothing_leaves_the_fault_state() {
        let mut ctx = Ctx::default();
        dispatch(&mut ctx, Event::Fault);
        assert_eq!(ctx.state, State::Fault);
        let before = ctx.unhandled;
        for e in [
            Event::Tick,
            Event::Block,
            Event::FeatureDone,
            Event::FrameReady,
            Event::TxOk,
            Event::TxFail,
            Event::Timeout,
            Event::Button,
            Event::Fault,
        ] {
            assert_eq!(dispatch(&mut ctx, e), None, "{:?} found a way out", e);
            assert_eq!(ctx.state, State::Fault);
        }
        assert_eq!(ctx.unhandled, before + 9);
    }

    /// The stub's bytes are P09's encoder's bytes, which is the criterion the
    /// two projects share. Asserted here against the crate P08 depends on, so a
    /// change to either is caught without the parity harness.
    #[test]
    fn the_stub_hands_over_exactly_what_p09_encodes() {
        let mut ctx = Ctx {
            pending_sample: -1,
            ..Default::default()
        };
        for e in [
            Event::Tick,
            Event::Block,
            Event::FeatureDone,
            Event::FrameReady,
        ] {
            dispatch(&mut ctx, e);
        }
        let mut want = [0u8; PAYLOAD_BYTES];
        encode(
            &mut want,
            &Payload {
                version: 1,
                flags: 0,
                sequence: 0,
                feature: -1,
                battery: 40,
            },
        )
        .unwrap();
        assert_eq!(&ctx.frame[..ctx.frame_len as usize], &want[..]);
    }

    #[test]
    fn the_sequence_wraps_where_p09_s_field_does() {
        let mut ctx = Ctx::default();
        ctx.sequence = 511;
        act_cycle_ok(&mut ctx);
        assert_eq!(ctx.sequence, 0, "the sequence field is nine bits");
    }
}
