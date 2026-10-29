//! P04's span arithmetic in Rust, against the same oracle as the C and the
//! Python.
//!
//! The same arithmetic, in the same order, as `../../c/rxspan.c`. Chapter 4's
//! receive path has no interrupt per byte: a transfer engine writes into a
//! circular buffer and the processor learns where it got to by reading the
//! transfer counter, so the whole of the difficult part is working out which
//! bytes are new, as at most two spans neither of which crosses the end of the
//! array. The file `../../c/rxspan.h` carries the reasoning and it is not
//! repeated here.
//!
//! # What this language adds: the count and the array cannot disagree
//!
//! The C returns an `int` that is 0, 1 or 2 and writes that many entries into
//! `out[RXSPAN_MAX]`. **Those two can disagree**, and a caller who reads
//! `out[1]` when the count is 1 gets whatever was in that slot before. Nothing
//! in the signature prevents it; the convention is that the return value says
//! how far to read.
//!
//! Here the answer is a [`Delivery`], which is `Nothing`, `One(span)` or
//! `Two(span, span)`. There is no count to disagree with anything, and no slot
//! to read that is not there. **That is the same move the C++ makes in P20**,
//! where each percentile is a `std::optional` rather than a value beside a flag
//! that can contradict it. Two instances in two languages on two projects, and
//! both are the same observation: where the C carries a convention, a sum type
//! carries a guarantee.
//!
//! What it is not: a better algorithm, fewer lines, or something the C could
//! not express with a tagged union. It removes one specific way for a caller to
//! be wrong.
//!
//! # The deliberate wrap, and the other side of a story told twice
//!
//! The advance is `(pos - old_pos)` masked, and on a `u16` that subtraction
//! underflows whenever the buffer has wrapped, which is the normal case this
//! function exists for. In Rust that panics in debug and in test, so the wrap
//! has to be spelled `wrapping_sub`.
//!
//! This volume has now met Rust's overflow checks three times, and the honest
//! summary is not that they catch defects:
//!
//! | | |
//! |---|---|
//! | P17, the instrument subtraction | a DEFECT; the check would have caught it in development |
//! | P20, the percentile rank product | a DEFECT; the check would have caught it in development |
//! | P04, here | CORRECT and intended; the check forces it to be named |
//!
//! Two of three were defects, which is why the earlier two READMEs say what
//! they say. The third is the useful generalisation: **the checks do not find
//! wraps, they force the author to declare which wraps are meant**, and a
//! `wrapping_sub` in the source is a statement a reviewer can disagree with
//! where the C's silence is not.
//!
//! This crate is `no_std`: every quantity is a fixed-width integer, so nothing
//! here needs a platform.

#![no_std]

/// At most two, because a circular buffer wraps at most once between two reads
/// of the position.
pub const MAX_SPANS: usize = 2;

/// A floor on sense rather than on correctness: the near-lap guard compares the
/// advance against half the buffer, and half of four bytes makes it
/// meaningless rather than wrong.
pub const MIN_SIZE: u16 = 8;

/// The same three refusals as the C, in the same order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The C's null pointer refusal, unreachable here because a `&mut` cannot
    /// be null. Kept so the three names mean the same thing in all four
    /// implementations and a filter answer never has to be translated.
    Argument,
    Size,
    Position,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Size => "size",
            Error::Position => "position",
        }
    }
}

/// An offset into the buffer and a length, where `off + len` never exceeds the
/// size, which is the plain pointer and length the chapter promises a consumer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Span {
    pub off: u16,
    pub len: u16,
}

/// What one step found. There is no count beside an array to disagree with it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Delivery {
    Nothing,
    One(Span),
    Two(Span, Span),
}

impl Delivery {
    /// The C's return value, for the filter and for the oracle. Derived from
    /// the shape rather than stored beside it.
    pub fn count(self) -> usize {
        match self {
            Delivery::Nothing => 0,
            Delivery::One(_) => 1,
            Delivery::Two(_, _) => 2,
        }
    }

    pub fn spans(self) -> [Option<Span>; MAX_SPANS] {
        match self {
            Delivery::Nothing => [None, None],
            Delivery::One(a) => [Some(a), None],
            Delivery::Two(a, b) => [Some(a), Some(b)],
        }
    }
}

/// The bound and the report the chapter offers in place of an overrun flag,
/// because this path has none: the arithmetic cannot tell a full lap from no
/// movement, since both arrive as `pos == old_pos`.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Stats {
    /// The largest one-step advance seen.
    pub peak_advance: u16,
    /// Steps that moved more than half the buffer.
    pub near_lap: u32,
    /// How many times the arithmetic ran, including steps that found nothing.
    pub calls: u32,
    /// Spans handed out, which is not the number of calls.
    pub deliveries: u32,
}

/// One step. `stats` is updated on every call that is not refused, including
/// calls that find nothing, because the count of calls is what the chapter's
/// half and full transfer interrupts exist to keep above a floor.
pub fn next_spans(
    size: u16,
    old_pos: u16,
    pos: u16,
    stats: &mut Stats,
) -> Result<Delivery, Error> {
    // A power of two, and the refusal is load bearing rather than decorative:
    // the advance is computed by masking, exactly as the chapter's step 7
    // does, and masking with size - 1 is the advance only when the size is a
    // power of two. A buffer of 500 bytes would produce a plausible wrong
    // number rather than an error.
    if size < MIN_SIZE || (size & (size - 1)) != 0 {
        return Err(Error::Size);
    }
    if old_pos >= size || pos >= size {
        return Err(Error::Position);
    }

    // Masked, not subtracted, and WRAPPING on purpose: a wrapped buffer makes
    // pos smaller than old_pos, which is the case this function exists for, so
    // the underflow is the mechanism rather than a mistake. The C relies on
    // unsigned wraparound being defined; Rust makes it say so.
    let advance = pos.wrapping_sub(old_pos) & (size - 1);

    stats.calls += 1;
    if advance > stats.peak_advance {
        stats.peak_advance = advance;
    }
    if advance > size / 2 {
        stats.near_lap += 1;
    }

    if pos == old_pos {
        return Ok(Delivery::Nothing);
    }

    let delivery = if pos > old_pos {
        Delivery::One(Span {
            off: old_pos,
            len: pos - old_pos,
        })
    } else {
        let tail = Span {
            off: old_pos,
            len: size - old_pos,
        };
        // Wrapping exactly onto zero is ONE span and not two, because a span
        // of no bytes at offset zero is not a delivery.
        if pos > 0 {
            Delivery::Two(tail, Span { off: 0, len: pos })
        } else {
            Delivery::One(tail)
        }
    };
    stats.deliveries += delivery.count() as u32;
    Ok(delivery)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn one(size: u16, old: u16, pos: u16) -> (Delivery, Stats) {
        let mut s = Stats::default();
        let d = next_spans(size, old, pos, &mut s).unwrap();
        (d, s)
    }

    #[test]
    fn nothing_new_is_still_a_call() {
        let (d, s) = one(512, 0, 0);
        assert_eq!(d, Delivery::Nothing);
        assert_eq!(s.calls, 1);
        assert_eq!(s.deliveries, 0, "a call is not a delivery");
        assert_eq!(s.peak_advance, 0);
    }

    #[test]
    fn a_wrap_is_the_tail_then_the_head() {
        let (d, s) = one(512, 500, 10);
        let tail = Span { off: 500, len: 12 };
        let head = Span { off: 0, len: 10 };
        assert_eq!(d, Delivery::Two(tail, head));
        assert_eq!(s.peak_advance, 22);
        assert_eq!(s.deliveries, 2);
    }

    /// Wrapping exactly onto zero is one span, because a span of no bytes is
    /// not a delivery. This is the case a count-and-array answer gets wrong by
    /// reporting two and leaving the second entry empty.
    #[test]
    fn wrapping_exactly_onto_zero_is_one_span() {
        let (d, s) = one(512, 500, 0);
        assert_eq!(d, Delivery::One(Span { off: 500, len: 12 }));
        assert_eq!(d.count(), 1);
        assert_eq!(s.deliveries, 1);
    }

    /// Half the buffer exactly is not a near lap and one byte past it is,
    /// which is the boundary the chapter's bound is stated at.
    #[test]
    fn the_near_lap_boundary_is_strictly_more_than_half() {
        let (_, s) = one(512, 0, 256);
        assert_eq!(s.near_lap, 0, "half exactly does not count");
        let (_, s) = one(512, 0, 257);
        assert_eq!(s.near_lap, 1, "one byte past half does");
    }

    /// The deliberate wrap, asserted rather than described. A plain subtraction
    /// here panics in debug and in test, so `wrapping_sub` is not a style
    /// choice.
    #[test]
    fn the_advance_across_a_wrap_uses_a_deliberate_underflow() {
        let (pos, old): (u16, u16) = (10, 500);
        assert_eq!(pos.wrapping_sub(old), 65046);
        assert_eq!(pos.wrapping_sub(old) & (512 - 1), 22);
        let (_, s) = one(512, old, pos);
        assert_eq!(s.peak_advance, 22, "the masked wrap is the advance");
    }

    #[test]
    fn the_refusals_are_the_cs_refusals() {
        let mut s = Stats::default();
        assert_eq!(next_spans(500, 0, 1, &mut s), Err(Error::Size));
        assert_eq!(next_spans(4, 0, 1, &mut s), Err(Error::Size));
        assert_eq!(next_spans(0, 0, 0, &mut s), Err(Error::Size));
        assert_eq!(next_spans(512, 512, 0, &mut s), Err(Error::Position));
        assert_eq!(next_spans(512, 0, 512, &mut s), Err(Error::Position));
        assert_eq!(s, Stats::default(), "a refused call must not count");
        assert_eq!(Error::Argument.name(), "argument");
    }
}
