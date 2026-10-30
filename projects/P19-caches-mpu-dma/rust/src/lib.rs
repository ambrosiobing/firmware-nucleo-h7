//! P19's alignment arithmetic: chapter 19's criteria 4 and 5, `no_std`.
//!
//! The same arithmetic, in the same order, as `../../c/mpualign.c` and
//! `../../python/mpualign.py`, against the same oracle. The four findings this
//! project is about are written out in
//! [`../../c/mpualign.h`](../../c/mpualign.h) and are not repeated here: a
//! clean may over-extend harmlessly where an invalidate discards a neighbour's
//! write, criterion 4's two halves fix different ends, the MPU's alignment is
//! to the region's own size and not to the cache line, and the end address can
//! wrap and then maintain nothing quietly.
//!
//! **P19 is the last of the twenty to carry a third and fourth language**, and
//! the only one whose C half already runs on the board: `c/board/icache.c` has
//! been flashed since Saturday 3 October 2026. This crate is the half that can
//! be compared, which is the same half the C and the Python compare.
//!
//! WHAT THIS CRATE ADDS IS THE WRAP CHECK MOVING INTO THE TYPE. The C has to
//! widen to 64 bits so that the test itself cannot wrap, and says so:
//!
//! ```c
//! const uint64_t last = (uint64_t) addr + (uint64_t) len;
//! const uint64_t aligned_end = (last + mask) & ~mask;
//! if (aligned_end >= 0x100000000ull) {
//!     return MPU_ERR_WRAP;
//! }
//! ```
//!
//! That is correct and it answers a question about 32 bit arithmetic by leaving
//! 32 bit arithmetic. Here the question is put to the 32 bit type itself:
//!
//! ```text
//! let last = addr.checked_add(len).ok_or(Error::Wrap)?;
//! let end = last.checked_next_multiple_of(line).ok_or(Error::Wrap)?;
//! ```
//!
//! **`checked_next_multiple_of` answers `None` exactly when the rounded end
//! would not fit in a `u32`**, which is finding four and its boundary in one
//! call: the case ending exactly at the top of the address space needs the
//! value 2 to the 32nd, a `u32` has only zero to offer, and storing zero IS the
//! failure rather than a near miss, because a loop from base to end then runs
//! no times. The C reaches the same refusal and has to compute in a wider type
//! to get there.
//!
//! The two forms agree on every case and on the boundary, which the oracle
//! pins, and the difference is not in the specification. It is recorded because
//! this is the fourth crate in the volume where the answer to "what does this
//! language add" is that **an arithmetic that cannot be expressed is refused by
//! the type rather than by a hand-written guard**, after P13's `Option<u8>`,
//! P11's `Option<u32>` and P15's absent default arm.
//!
//! AND ONE LOOP BECOMES A CALL. The C counts the region size's logarithm with a
//! shift loop; this calls `u32::ilog2`. Both answer the same number for every
//! power of two, which is the only input either reaches, since both refuse a
//! size that is not one before computing it. P16's crate made the same kind of
//! swap, `u64::isqrt` for a bit-by-bit descent, and there it was the one
//! quantity where the four implementations had real room to disagree; here
//! there is none, because the input is a power of two by the time the call
//! happens.
#![cfg_attr(not(test), no_std)]

/// This part's cache line, measured by P19's placement experiment on Saturday
/// 3 October 2026: the cold cost tracked the offset within a 32 byte granule,
/// 3220.99 cycles at offset 0 against 3163.98 at offset 16, and was flat at
/// 2378.99 across all four offsets once cached. **Every function still takes
/// the line as an argument**, because a figure measured on one part is not a
/// constant of the arithmetic.
pub const LINE: u32 = 32;

/// ARMv7-M's smallest region.
pub const MIN_REGION: u32 = 32;

/// The largest region this signature can express. The architecture also allows
/// a region covering the whole address space, which is 2 to the 32nd and does
/// not fit in a `u32`, so a caller who needs it needs a different signature.
pub const MAX_REGION: u32 = 0x8000_0000;

/// The same six refusals as the C, in the same order and with its names.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The C's null pointer. **Unreachable here**, because a reference cannot
    /// be null and the answers are returned by value; the name is kept so the
    /// six refusals mean the same thing in all four implementations.
    Argument,
    /// A line length of zero, or one that is not a power of two.
    Line,
    /// A buffer of no bytes, which has no range to maintain.
    Length,
    /// The end address does not fit in 32 bits, which is finding four.
    Wrap,
    /// A region size of zero, not a power of two, below the minimum, or above
    /// what this signature can express.
    Size,
    /// A region base that is not a multiple of its own size, which is finding
    /// three, or a buffer sharing a cache line, which is criterion 4.
    Align,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Line => "line",
            Error::Length => "length",
            Error::Wrap => "wrap",
            Error::Size => "size",
            Error::Align => "align",
        }
    }
}

/// The whole lines a maintenance operation touches, with the two shared lines
/// reported separately so a caller can be refused for the operation that
/// cannot tolerate them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Range {
    /// The aligned start of the maintenance range.
    pub base: u32,
    /// One past the aligned end.
    pub end: u32,
    pub bytes: u32,
    pub lines: u32,
    /// The first line also holds bytes before the buffer, so invalidating it
    /// discards them.
    pub head_shared: bool,
    /// The last line also holds bytes after the buffer.
    pub tail_shared: bool,
}

fn is_power_of_two(v: u32) -> bool {
    v != 0 && (v & (v - 1)) == 0
}

/// The whole lines a maintenance operation over `[addr, addr + len)` touches.
pub fn maint_range(addr: u32, len: u32, line: u32) -> Result<Range, Error> {
    // A power of two, because the rounding is a mask and not a division, and
    // because no cache has a line that is not one.
    if !is_power_of_two(line) {
        return Err(Error::Line);
    }
    if len == 0 {
        return Err(Error::Length);
    }

    // Finding four, asked of the 32 bit type rather than of a wider one: both
    // calls answer None exactly when the value would not fit, and the end is
    // one past the last address, so a range reaching the top of the address
    // space has no u32 to hold it. The crate comment says why storing zero
    // there would be the failure rather than an approximation of it.
    let last = addr.checked_add(len).ok_or(Error::Wrap)?;
    let end = last.checked_next_multiple_of(line).ok_or(Error::Wrap)?;

    let mask = line - 1;
    let base = addr & !mask;
    Ok(Range {
        base,
        end,
        bytes: end - base,
        lines: (end - base) / line,
        // The two shared lines, which finding one is about: the head when the
        // buffer does not start on a line, the tail when it does not end on
        // one.
        head_shared: (addr & mask) != 0,
        tail_shared: (last & mask) != 0,
    })
}

/// Criterion 4, as one answer: aligned to a line **and** a whole number of
/// lines. A shared line in either position is a failure of alignment, which is
/// the criterion's own word for it.
pub fn buffer_check(addr: u32, len: u32, line: u32) -> Result<(), Error> {
    let r = maint_range(addr, len, line)?;
    if r.head_shared || r.tail_shared {
        return Err(Error::Align);
    }
    Ok(())
}

/// Criterion 5: an ARMv7-M region's SIZE field, which is log2(size) minus one,
/// with the base checked against the region's **own** size.
pub fn region_encode(base: u32, size: u32) -> Result<u8, Error> {
    // No upper test, and the absence is deliberate: every power of two a u32
    // can hold is at or below 2 to the 31st, which is MAX_REGION, so a size
    // above it cannot arrive here and be a power of two. A size > MAX_REGION
    // test would be a guard that can never fire, and a check that cannot fail
    // is worse than no check because it reads as coverage.
    if !is_power_of_two(size) || size < MIN_REGION {
        return Err(Error::Size);
    }
    // Finding three: the base must be a multiple of the region's own size and
    // not of the cache line, so a buffer that satisfies criterion 4 can still
    // fail here.
    if (base & (size - 1)) != 0 {
        return Err(Error::Align);
    }
    // ilog2 where the C counts shifts. The input is a power of two by now, so
    // the two agree on every value either can reach.
    Ok((size.ilog2() - 1) as u8)
}

#[cfg(test)]
mod tests {
    use super::*;

    const SRAM: u32 = 0x2400_0000;

    #[test]
    fn a_buffer_aligned_and_a_whole_number_of_lines_shares_no_line() {
        let r = maint_range(SRAM, 64, LINE).expect("criterion 4 satisfied");
        assert_eq!((r.base, r.end), (SRAM, SRAM + 64));
        assert_eq!((r.bytes, r.lines), (64, 2));
        assert!(!r.head_shared && !r.tail_shared);
        assert_eq!(buffer_check(SRAM, 64, LINE), Ok(()));
    }

    #[test]
    fn criterion_four_has_two_halves_and_they_fix_different_ends() {
        // Aligned and not a multiple: the TAIL is shared.
        let r = maint_range(SRAM, 40, LINE).expect("a range is still returned");
        assert!(!r.head_shared && r.tail_shared);
        // Neither aligned nor a multiple: BOTH are shared.
        let r = maint_range(SRAM + 16, 32, LINE).expect("a range");
        assert!(r.head_shared && r.tail_shared);
        // And both are failures of criterion 4.
        assert_eq!(buffer_check(SRAM, 40, LINE), Err(Error::Align));
        assert_eq!(buffer_check(SRAM + 16, 32, LINE), Err(Error::Align));
    }

    #[test]
    fn the_end_that_cannot_be_expressed_is_refused_and_not_stored_as_zero() {
        // A buffer ending exactly at the top of the address space: the end is
        // one past the last address, so the honest value is 2 to the 32nd.
        assert_eq!(maint_range(0xFFFF_FFE0, 32, LINE), Err(Error::Wrap));
        // And one whose unrounded sum already wraps.
        assert_eq!(maint_range(0xFFFF_FFF0, 32, LINE), Err(Error::Wrap));
        // One line lower is fine, which is what makes the boundary a boundary.
        let r = maint_range(0xFFFF_FFC0, 32, LINE).expect("inside the space");
        assert_eq!(r.end, 0xFFFF_FFE0);
    }

    #[test]
    fn the_wrap_refusal_survives_a_rounding_that_crosses_the_boundary() {
        // The unrounded sum fits and the ROUNDED end does not, which is the
        // case a 32 bit `addr + len < addr` test would pass.
        assert_eq!(maint_range(0xFFFF_FFC1, 32, LINE), Err(Error::Wrap));
    }

    #[test]
    fn the_region_size_field_is_log2_minus_one() {
        assert_eq!(region_encode(SRAM, 32), Ok(4));
        assert_eq!(region_encode(SRAM + 0x1000, 4096), Ok(11));
        assert_eq!(region_encode(0x8000_0000, MAX_REGION), Ok(30));
    }

    #[test]
    fn finding_three_is_that_criterion_four_says_nothing_about_criterion_five() {
        // 32 byte aligned, so criterion 4 is satisfied exactly.
        assert_eq!(buffer_check(SRAM + 0x100, 4096, LINE), Ok(()));
        // And an ARMv7-M region must start on a multiple of its own size.
        assert_eq!(region_encode(SRAM + 0x100, 4096), Err(Error::Align));
    }

    #[test]
    fn the_refusals_are_the_cs_refusals() {
        assert_eq!(maint_range(SRAM, 32, 0), Err(Error::Line));
        assert_eq!(maint_range(SRAM, 32, 48), Err(Error::Line));
        assert_eq!(maint_range(SRAM, 0, LINE), Err(Error::Length));
        assert_eq!(region_encode(SRAM, 0), Err(Error::Size));
        assert_eq!(region_encode(SRAM, 16), Err(Error::Size));
        assert_eq!(region_encode(SRAM, 48), Err(Error::Size));
    }
}
