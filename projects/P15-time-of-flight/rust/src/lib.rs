//! P15's hysteresis: the four states of chapter 15 figure 15.4, `no_std`.
//!
//! The same state machine, with the same edges in the same order, as
//! `../../c/zonetrack.c` and `../../python/zonetrack.py`, against the same
//! oracle. The reasoning is in [`../../c/zonetrack.h`](../../c/zonetrack.h) and
//! is not repeated here, including which source the specification comes from:
//! step 9 gives two thresholds and the confirmation count, figure 15.4 gives
//! those and a third threshold of 1500 mm that the sketch omits, and **the
//! figure is the more complete of the two**, so the figure is what all four
//! implementations follow.
//!
//! WHAT THIS CRATE ADDS IS AN ARM THAT DOES NOT EXIST. The C's `switch` over
//! the state ends with
//!
//! ```c
//! default:
//!     return ZONETRACK_ERR_ARGUMENT;
//! ```
//!
//! because `zonetrack_state_t` is an enum in a struct a caller can memset, scribble
//! on or copy from a buffer, so a value outside the four is representable and
//! has to be answered. **A `match` over this enum is exhaustive over exactly
//! four variants**, so that arm is not written here, the compiler refuses the
//! file if a variant is ever added without a new arm, and the refusal path for
//! an impossible state does not exist.
//!
//! The other half of `Error::Argument` is the C's null tracker and null policy,
//! which a reference cannot be. So **`Error::Argument` is unreachable in this
//! crate**, in both of the ways the C can reach it, and the oracle has no case
//! for it. The name is kept so the two refusals mean the same thing in all
//! four implementations, which is the volume's rule, and this comment is the
//! honest half of it: a refusal that cannot happen is not a refusal that was
//! tested.
//!
//! That is the same shape as P07's `kRoom` in C++ and P13's `Option<u8>`
//! replacing a flag beside a counter: the language removes a state rather than
//! the arithmetic.
//!
//! THE WINDOW IS CLEARED ON EVERY STATE CHANGE, which is a decision the chapter
//! does not make and all four implementations share: votes gathered while
//! approaching say nothing about whether to leave, and carrying them over would
//! let a target that crossed 800 mm three times on the way in satisfy the exit
//! test on the way out.
#![cfg_attr(not(test), no_std)]

/// The window is a bitmask, so the frames it can hold is the bits in it.
/// Sixteen is more than chapter 15 needs, and the policy refusal is what keeps
/// a caller from asking for more.
pub const WINDOW_MAX: u8 = 16;

/// A frame with no valid zone at all, treated as further away than any
/// threshold.
pub const NO_TARGET: u16 = 0xFFFF;

/// The four states, numbered as chapter 15's host decoder indexes them:
/// `["clear", "approaching", "present", "leaving"][(b >> 5) & 3]`. **The
/// numbering is part of the wire format**, not an internal detail, which is why
/// the discriminants are written out.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    Clear = 0,
    Approaching = 1,
    Present = 2,
    Leaving = 3,
}

impl State {
    pub fn name(self) -> &'static str {
        match self {
            State::Clear => "clear",
            State::Approaching => "approaching",
            State::Present => "present",
            State::Leaving => "leaving",
        }
    }

    /// The wire value, which a host decoder reads out of bits 5 and 6.
    pub fn code(self) -> u8 {
        self as u8
    }
}

/// The same two refusals as the C, with its names.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The C's null tracker, null policy or impossible state. **Unreachable
    /// here**: see the crate comment.
    Argument,
    /// Thresholds out of order, or a window of zero, above the maximum, or
    /// with more agreeing frames required than the window holds.
    Policy,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Policy => "policy",
        }
    }
}

/// The policy, passed rather than compiled in, because the chapter's own note
/// on figure 15.4 calls the numbers "a starting point measured against a real
/// target rather than constants to be trusted".
#[derive(Debug, Clone, Copy)]
pub struct Policy {
    /// The gate between clear and approaching.
    pub far_mm: u16,
    /// Approaching to present, with confirmation.
    pub enter_mm: u16,
    /// Present to leaving, with confirmation.
    pub leave_mm: u16,
    /// Agreeing frames needed.
    pub confirm_n: u8,
    /// Out of how many, the window.
    pub confirm_m: u8,
}

impl Policy {
    fn is_sane(&self) -> bool {
        if self.confirm_m == 0 || self.confirm_m > WINDOW_MAX {
            return false;
        }
        if self.confirm_n == 0 || self.confirm_n > self.confirm_m {
            return false;
        }
        // Enter is nearer than leave, and leave is nearer than far. A policy
        // with leave below enter has no hysteresis at all: the two thresholds
        // would cross and the state would chatter at the crossing, which is
        // the single thing this whole file exists to prevent.
        self.enter_mm < self.leave_mm && self.leave_mm < self.far_mm
    }
}

#[derive(Debug, Clone, Copy)]
pub struct Tracker {
    pub state: State,
    /// One bit per frame in the window, newest at bit 0.
    pub votes: u16,
    /// Frames in the window, saturating at `confirm_m`.
    pub seen: u8,
    /// Every frame this tracker has been given.
    pub frames: u32,
    /// State changes, which is the figure a policy is judged by: hundreds in
    /// an hour means the thresholds are too close together.
    pub changes: u32,
    /// A full window has been seen at least once.
    pub ready: bool,
}

impl Default for Tracker {
    fn default() -> Self {
        Tracker {
            state: State::Clear,
            votes: 0,
            seen: 0,
            frames: 0,
            changes: 0,
            ready: false,
        }
    }
}

impl Tracker {
    /// A tracker in the clear state with an empty window. The policy is
    /// validated by [`Tracker::update`] rather than here, because a tracker
    /// outlives the call that made it.
    pub fn new() -> Self {
        Tracker::default()
    }

    /// Push one vote into the window and report how many of the last
    /// `confirm_m` frames voted yes.
    ///
    /// The C counts the bits in a loop because C has no portable population
    /// count; this calls `count_ones`. The two produce the same count, and
    /// only the count reaches the comparison, so the difference is not in the
    /// specification.
    fn push_vote(&mut self, p: &Policy, yes: bool) -> u32 {
        let keep: u32 = if p.confirm_m >= 32 {
            0xFFFF_FFFF
        } else {
            (1u32 << p.confirm_m) - 1
        };
        let window = ((u32::from(self.votes) << 1) | u32::from(yes)) & keep;
        self.votes = window as u16;

        if self.seen < p.confirm_m {
            self.seen += 1;
            if self.seen == p.confirm_m {
                self.ready = true;
            }
        }
        window.count_ones()
    }

    fn enter(&mut self, next: State) {
        if next != self.state {
            self.state = next;
            self.changes += 1;
        }
        // Cleared on every change, including a change into the same state,
        // which cannot happen here but would be the quiet kind of defect if it
        // could. The crate comment says why votes do not carry across an edge.
        self.votes = 0;
        self.seen = 0;
    }

    /// Feed one frame's nearest valid distance in millimetres and get the state
    /// after it. A frame with no valid zone at all is [`NO_TARGET`].
    pub fn update(&mut self, p: &Policy, nearest_mm: u16) -> Result<State, Error> {
        if !p.is_sane() {
            return Err(Error::Policy);
        }
        self.frames += 1;

        match self.state {
            State::Clear => {
                // Immediate, with no confirmation: the figure draws no count
                // on this edge, and a node that waited three frames to notice
                // something is in the room would be slower than the sensor.
                if nearest_mm < p.far_mm {
                    self.enter(State::Approaching);
                }
            }
            State::Approaching => {
                if self.push_vote(p, nearest_mm < p.enter_mm) >= u32::from(p.confirm_n) {
                    self.enter(State::Present);
                } else if nearest_mm >= p.far_mm {
                    // Went away again without ever confirming. The figure has
                    // no edge for this, and leaving the node in approaching
                    // for ever after a target walks out of range would be
                    // worse, so it returns to clear.
                    self.enter(State::Clear);
                }
            }
            State::Present => {
                if self.push_vote(p, nearest_mm > p.leave_mm) >= u32::from(p.confirm_n) {
                    self.enter(State::Leaving);
                }
            }
            State::Leaving => {
                if nearest_mm > p.far_mm {
                    self.enter(State::Clear);
                } else if nearest_mm < p.enter_mm {
                    // Came back. The figure has no edge for this either, and a
                    // target that returns while the node still calls it
                    // leaving should be present again rather than waiting to
                    // pass 1500 mm first.
                    self.enter(State::Present);
                }
            }
        }
        Ok(self.state)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The chapter's own numbers: far 1500, enter 800, leave 950, three of
    /// five.
    fn policy() -> Policy {
        Policy {
            far_mm: 1500,
            enter_mm: 800,
            leave_mm: 950,
            confirm_n: 3,
            confirm_m: 5,
        }
    }

    fn walk(distances: &[u16]) -> (Tracker, [State; 16]) {
        let p = policy();
        let mut t = Tracker::new();
        let mut out = [State::Clear; 16];
        for (i, d) in distances.iter().enumerate() {
            out[i] = t.update(&p, *d).expect("the chapter's policy is sane");
        }
        (t, out)
    }

    #[test]
    fn crossing_the_far_gate_enters_approaching_at_once() {
        let (t, states) = walk(&[1499]);
        assert_eq!(states[0], State::Approaching);
        assert_eq!(t.changes, 1);
    }

    #[test]
    fn three_of_five_under_the_near_threshold_confirms_present() {
        let (_, states) = walk(&[1000, 700, 700, 1000, 700]);
        assert_eq!(states[0], State::Approaching);
        assert_eq!(states[4], State::Present);
        // Two of five is not enough, however close they are to the threshold.
        let (_, two) = walk(&[1000, 700, 700, 1000, 1000]);
        assert_eq!(two[4], State::Approaching);
    }

    #[test]
    fn the_thresholds_are_strict_so_sitting_on_one_never_crosses_it() {
        // 800 is not under 800, so it never confirms present.
        let (_, states) = walk(&[1000, 800, 800, 800, 800, 800]);
        assert_eq!(states[5], State::Approaching);
    }

    #[test]
    fn the_window_is_cleared_on_every_edge() {
        // Three under 800 confirm present; the three votes must not then
        // count toward leaving.
        let p = policy();
        let mut t = Tracker::new();
        for d in [1000u16, 700, 700, 700] {
            t.update(&p, d).expect("sane");
        }
        assert_eq!(t.state, State::Present);
        assert_eq!(t.votes, 0, "the votes that confirmed present are gone");
        assert_eq!(t.seen, 0);
    }

    #[test]
    fn a_target_that_returns_while_leaving_is_present_again() {
        let (t, states) = walk(&[1000, 700, 700, 700, 1000, 1000, 1000, 700]);
        assert_eq!(states[6], State::Leaving);
        assert_eq!(states[7], State::Present);
        assert!(t.changes >= 3);
    }

    #[test]
    fn the_policy_refusals_are_the_cs_refusals() {
        let mut t = Tracker::new();
        let mut p = policy();
        p.leave_mm = 700; // below enter, so no hysteresis at all
        assert_eq!(t.update(&p, 1000), Err(Error::Policy));
        let mut p = policy();
        p.confirm_m = 0;
        assert_eq!(t.update(&p, 1000), Err(Error::Policy));
        let mut p = policy();
        p.confirm_m = WINDOW_MAX + 1;
        assert_eq!(t.update(&p, 1000), Err(Error::Policy));
        let mut p = policy();
        p.confirm_n = p.confirm_m + 1;
        assert_eq!(t.update(&p, 1000), Err(Error::Policy));
    }

    #[test]
    fn no_target_is_further_away_than_any_threshold() {
        let (_, states) = walk(&[NO_TARGET, NO_TARGET]);
        assert_eq!(states[1], State::Clear);
    }

    #[test]
    fn the_state_numbering_is_the_wire_format() {
        // A host decoder reads ["clear", "approaching", "present",
        // "leaving"][(b >> 5) & 3], so these four values are a contract.
        assert_eq!(State::Clear.code(), 0);
        assert_eq!(State::Approaching.code(), 1);
        assert_eq!(State::Present.code(), 2);
        assert_eq!(State::Leaving.code(), 3);
    }
}
