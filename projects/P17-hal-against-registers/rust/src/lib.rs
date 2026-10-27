//! P17's timebase arithmetic in Rust, against the same oracle as the C and the
//! Python.
//!
//! The same arithmetic in the same order as `../../c/tbmath.c`: elapsed ticks
//! across a wrap and in either direction, the wrap period from a width and a
//! frequency, the instrument's own cost subtracted, and the first run kept apart
//! from the median of the remainder.
//!
//! # What this language says about chapter 17's own defect
//!
//! Step 6 subtracts the calibrated cost of reading the counter with
//! `(b - a) - cyc_overhead` on a `uint32_t`. In C an interval below that cost
//! wraps to 4294967291 ticks, which at the core clock is 15.339 seconds, and
//! that is exactly one wrap of the same counter so it does not look like an
//! error. The same expression in Rust behaves differently, and the difference is
//! narrower than the usual claim about this language:
//!
//! * **Under `cargo test` and in any debug build it panics**, with "attempt to
//!   subtract with overflow". Cargo's dev profile sets `overflow-checks` from
//!   `debug-assertions`, which is on, and this workspace does not override it.
//!   A test below asserts that panic rather than describing it.
//! * **In the release profile this workspace builds, it wraps**, exactly as the
//!   C does, because `overflow-checks` defaults to off there and the manifest
//!   does not turn it on. A second test shows the wrapped value through
//!   `wrapping_sub`, which is profile independent.
//!
//! So the honest statement is that Rust's arithmetic checks would have caught
//! this during development and not in the field, which is a smaller claim than
//! "this cannot happen in Rust" and the only one the profiles support. The
//! checked form, `checked_sub`, is what this crate actually uses, and it is
//! available in C too: the C refuses on the same condition with an explicit
//! comparison. The language did not make the fix possible; it made the unfixed
//! version noisy in one profile.
//!
//! This crate is `no_std`: nothing here needs an allocator or a platform. The
//! wrap period divides by a frequency and `f64` division is in `core`, unlike
//! `sqrt` and `log10`, which is why P06's and P18's crates are `std` and this
//! one is not.

#![no_std]

/// Enough samples for the chapter's calibration loop of 64 and its repetition
/// counts, with room for the first run to be held out.
pub const MAX_SAMPLES: usize = 256;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Direction {
    Up,
    Down,
}

/// The same seven refusals as the C, in the same order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Argument,
    Width,
    Direction,
    Range,
    Rate,
    Underflow,
    Samples,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Width => "width",
            Error::Direction => "direction",
            Error::Range => "range",
            Error::Rate => "rate",
            Error::Underflow => "underflow",
            Error::Samples => "samples",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Report {
    pub first: u32,
    pub median: u32,
    pub smallest: u32,
    pub largest: u32,
    pub count: u32,
}

fn mask_of(width_bits: u32) -> u32 {
    if width_bits >= 32 {
        u32::MAX
    } else {
        (1_u32 << width_bits) - 1
    }
}

/// Ticks between two readings of one counter, across a wrap and in either
/// direction.
///
/// The direction is an argument because chapter 17's own timebase struct has no
/// field for it while its own comment says one of its three counters counts
/// down. `wrapping_sub` is explicit here for the same reason the C masks: one
/// wrap is handled and two are indistinguishable from none, which is the bound
/// `wrap_ns` reports.
pub fn elapsed(start: u32, end: u32, width_bits: u32, dir: Direction) -> Result<u32, Error> {
    if width_bits == 0 || width_bits > 32 {
        return Err(Error::Width);
    }
    let mask = mask_of(width_bits);
    if (start & mask) != start || (end & mask) != end {
        return Err(Error::Range);
    }
    let difference = match dir {
        Direction::Down => start.wrapping_sub(end) & mask,
        Direction::Up => end.wrapping_sub(start) & mask,
    };
    Ok(difference)
}

/// The wrap period in nanoseconds, from the tick frequency and the width.
pub fn wrap_ns(hz: f64, width_bits: u32) -> Result<u64, Error> {
    if width_bits == 0 || width_bits > 32 {
        return Err(Error::Width);
    }
    if !(hz > 0.0) {
        return Err(Error::Rate);
    }
    let ticks = 1_u64 << width_bits;
    let ns = (ticks as f64) * 1.0e9 / hz;
    Ok((ns + 0.5) as u64)
}

/// A raw interval less the instrument's own cost.
///
/// `checked_sub` rather than `-`: the module comment explains at length why the
/// unchecked form is the chapter's defect and why its behaviour here depends on
/// the profile.
pub fn subtract_overhead(raw: u32, overhead: u32) -> Result<u32, Error> {
    raw.checked_sub(overhead).ok_or(Error::Underflow)
}

/// The first run and the median of the remainder, as two numbers.
pub fn report(samples: &[u32]) -> Result<Report, Error> {
    if samples.len() > MAX_SAMPLES || samples.len() < 2 {
        return Err(Error::Samples);
    }
    let mut rest = [0_u32; MAX_SAMPLES];
    let count = samples.len() - 1;
    rest[..count].copy_from_slice(&samples[1..]);
    let slice = &mut rest[..count];
    slice.sort_unstable();

    let median = if count % 2 == 1 {
        slice[count / 2]
    } else {
        let sum = u64::from(slice[count / 2 - 1]) + u64::from(slice[count / 2]);
        (sum / 2) as u32
    };
    Ok(Report {
        first: samples[0],
        median,
        smallest: slice[0],
        largest: slice[count - 1],
        count: count as u32,
    })
}

/// Chapter 17 step 6 as printed, with the wrap made explicit so the value is the
/// same in every profile. Nothing above calls it.
pub fn chapter_measure_run(a: u32, b: u32, overhead: u32) -> u32 {
    b.wrapping_sub(a).wrapping_sub(overhead)
}

/// Chapter 17 step 8's elapsed interval, through a struct carrying a width and a
/// frequency and no direction. Nothing above calls it.
pub fn chapter_timebase_elapsed(start: u32, end: u32, width_bits: u32) -> u32 {
    end.wrapping_sub(start) & mask_of(width_bits)
}

#[cfg(test)]
mod tests {
    use super::*;

    const CORE_HZ: f64 = 280.0e6;

    #[test]
    fn the_chapters_three_wrap_periods_are_right() {
        let cycle = wrap_ns(CORE_HZ, 32).unwrap();
        let timer = wrap_ns(1.0e6, 32).unwrap();
        let systick = wrap_ns(CORE_HZ, 24).unwrap();
        assert_eq!(cycle, 15_339_168_914);
        assert_eq!(timer, 4_294_967_296_000);
        assert_eq!(systick, 59_918_629);
    }

    #[test]
    fn the_direction_is_the_field_step_8_does_not_have() {
        let known = elapsed(1000, 900, 24, Direction::Down).unwrap();
        assert_eq!(known, 100);
        let through_struct = chapter_timebase_elapsed(1000, 900, 24);
        assert_eq!(through_struct, 16_777_116);
    }

    /// The defect as the chapter writes it, in the profile `cargo test` uses.
    ///
    /// Cargo's dev profile sets overflow-checks from debug-assertions, which is
    /// on, and this workspace does not override it. So the plain subtraction
    /// panics here. It does NOT panic in this workspace's release profile,
    /// where overflow-checks is off, and the test below shows what it produces
    /// there.
    #[test]
    #[should_panic(expected = "attempt to subtract with overflow")]
    fn the_plain_subtraction_panics_under_test() {
        let raw: u32 = 5;
        let overhead: u32 = 10;
        let _ = raw - overhead;
    }

    /// And what the same expression produces where the checks are off, which is
    /// this workspace's release profile and every C build.
    #[test]
    fn the_wrapped_value_is_one_whole_wrap_of_the_counter() {
        let wrapped = chapter_measure_run(0, 5, 10);
        assert_eq!(wrapped, 4_294_967_291);
        let seconds = f64::from(wrapped) / CORE_HZ;
        let wrap = wrap_ns(CORE_HZ, 32).unwrap() as f64 / 1.0e9;
        // The wrong answer is the size of one whole wrap, which is why it does
        // not look like an error. Written as two comparisons rather than with
        // f64::abs, which lives in std and not in core for the same reason
        // f64::sqrt does, and this crate is no_std. P12's gate made exactly
        // this substitution to stay no_std, and its README records it.
        let apart = if seconds > wrap {
            seconds - wrap
        } else {
            wrap - seconds
        };
        assert!(apart / wrap < 1.0e-6);
    }

    #[test]
    fn the_checked_form_refuses_instead() {
        assert_eq!(subtract_overhead(5, 10), Err(Error::Underflow));
        assert_eq!(subtract_overhead(10, 10), Ok(0));
        assert_eq!(subtract_overhead(100, 10), Ok(90));
    }

    #[test]
    fn the_refusals_are_the_cs_refusals() {
        let up = Direction::Up;
        assert_eq!(elapsed(0, 0, 0, up), Err(Error::Width));
        assert_eq!(elapsed(0, 0, 33, up), Err(Error::Width));
        assert_eq!(elapsed(0x100_0000, 0, 24, up), Err(Error::Range));
        assert_eq!(wrap_ns(0.0, 32), Err(Error::Rate));
        assert_eq!(wrap_ns(CORE_HZ, 0), Err(Error::Width));
        assert_eq!(report(&[100]), Err(Error::Samples));
        assert_eq!(report(&[]), Err(Error::Samples));
    }

    #[test]
    fn the_first_run_is_kept_apart_and_not_assumed_slowest() {
        let got = report(&[50, 1000, 60]).unwrap();
        assert_eq!(got.first, 50);
        assert_eq!(got.median, 530);
        assert_eq!(got.count, 2);
    }
}
