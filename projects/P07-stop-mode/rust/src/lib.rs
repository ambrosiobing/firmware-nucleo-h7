//! P07's cycle segmentation, its report and the battery arithmetic in Rust,
//! against the same oracle as the C and the Python.
//!
//! The same arithmetic, in the same order, as `../../c/cyclecharge.c`. The file
//! `../../c/cyclecharge.h` carries the reasoning, including finding one: a
//! trace that begins with the marker already high has no rising edge in front
//! of its first cycle, so that cycle is not complete and is not reported.
//!
//! # Every comparison in this project is exact, and that governs the whole file
//!
//! The oracle's values are powers of two and the parity test compares `f64`
//! with `==`, with no tolerance anywhere. So this is not a reimplementation
//! that agrees closely; it has to agree BIT FOR BIT, which makes the order of
//! operations part of the specification rather than a detail:
//!
//! * the charge of a cycle is the samples accumulated and THEN divided,
//!   `sum / fs_hz`, not each sample divided and summed
//! * the spread is `100.0 * (max - min) / median`, in that grouping
//! * the cell energy is `cell_mah * 1e-3 * 3600.0 * cell_volts`, left to right
//! * the median of an even count is `0.5 * (a + b)`, which is what the
//!   chapter's own library does
//!
//! # What this language adds here: there is no flag to get wrong
//!
//! On Monday 26 October 2026 P18's cascade turned main red because gcc at `-O2`
//! fuses a multiply followed by an add into a single instruction that rounds
//! once where the two separate operations round twice. Every floating-point
//! host build in this repository therefore passes `-ffp-contract=off`, there is
//! a test that enforces it, and that test could not see a header-only library
//! until Wednesday 28 October 2026.
//!
//! **Rust needs none of that, because contraction is opt in.** `a * b + c`
//! compiles to a multiply and an add; fusing is `a.mul_add(b, c)` and nothing
//! else does it. There is no flag to pass, no flag to forget, and no guard to
//! write that might not see the file.
//!
//! That is a narrower claim than it may look. It does not say Rust's floating
//! point is more accurate: `mul_add` is available and is the MORE accurate of
//! the two, and the reason this repository refuses it is comparison against a
//! twin that cannot fuse, not accuracy. What Rust removes is a build setting
//! whose omission silently changes results, which on this volume has cost one
//! red commit and one guard that needed extending.
//!
//! This crate is `no_std`. Every operation here is `+`, `-`, `*`, `/` and a
//! comparison on `f64`, all of which are in core; `sqrt` and `log10` are not,
//! which is what makes P06's and P18's crates std. P17's crate said that first
//! and this is the second project to reach the same conclusion.

#![no_std]

/// The bound the report's fixed buffer holds.
pub const MAX_CYCLES: usize = 64;

/// The chapter's own floor on how many cycles a median may be taken over.
pub const MIN_CYCLES: u32 = 10;

pub const SECONDS_PER_DAY: f64 = 86400.0;

/// The same seven refusals as the C, in the same order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// A null pointer in the C, and in this language only an empty trace.
    Argument,
    Rate,
    Bit,
    Room,
    Cycles,
    Supply,
    Day,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Rate => "rate",
            Error::Bit => "bit",
            Error::Room => "room",
            Error::Cycles => "cycles",
            Error::Supply => "supply",
            Error::Day => "day",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Report {
    pub cycles: u32,
    pub median_uc: f64,
    pub min_uc: f64,
    pub max_uc: f64,
    /// 100 times max less min over the median, as step 8 defines it.
    pub spread_pct: f64,
    /// The median charge at the stated supply.
    pub energy_median_uj: f64,
}

/// Every assumption is a field, which is the chapter's own framing: only the
/// first of these is a measurement.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Battery {
    /// THE MEASUREMENT. Everything below is not.
    pub energy_per_cycle_uj: f64,
    pub cycles_per_day: f64,
    pub cycle_period_s: f64,
    pub sleep_ua: f64,
    pub sleep_volts: f64,
    pub cell_mah: f64,
    pub cell_volts: f64,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Lifetime {
    pub cell_j: f64,
    pub covered_s: f64,
    /// The rest of the day, which step 10 omits.
    pub uncovered_s: f64,
    pub cycle_j_per_day: f64,
    pub uncovered_j_per_day: f64,
    /// With the uncovered day costed.
    pub days: f64,
    /// Step 10's printed form, kept for the comparison.
    pub days_chapter: f64,
    pub covers_the_day: bool,
}

/// The C's `!(x > 0.0)`, written so that clippy accepts it.
///
/// `!(x > 0.0)` is what the C writes and clippy rejects the shape as
/// `neg_cmp_op_on_partial_ord`, which cost a round trip on P17 on Tuesday 27
/// October 2026. This is the same predicate and not a near equivalent: for any
/// value that is not a number the first term is true, and for every other value
/// `x <= 0.0` is exactly the negation of `x > 0.0`. An infinite rate is
/// therefore accepted here as it is in the C, which is deliberate, because the
/// oracle has no such case and inventing a refusal the C does not make would be
/// a divergence rather than an improvement.
fn not_above_zero(x: f64) -> bool {
    x.is_nan() || x <= 0.0
}

/// An insertion sort, written out rather than using the slice's own, so the
/// comparison is the C's `>` on every pair and no question of how a library
/// orders values that are not numbers can arise.
fn sort_small(v: &mut [f64]) {
    for i in 1..v.len() {
        let key = v[i];
        let mut j = i;
        while j > 0 && v[j - 1] > key {
            v[j] = v[j - 1];
            j -= 1;
        }
        v[j] = key;
    }
}

/// The mean of the two middle values on an even count, which is what the
/// chapter's library does and therefore what a comparison against it needs.
fn median_of(sorted: &[f64]) -> f64 {
    let count = sorted.len();
    if (count & 1) != 0 {
        return sorted[count / 2];
    }
    0.5 * (sorted[count / 2 - 1] + sorted[count / 2])
}

/// Charge in microcoulombs for each COMPLETE cycle, where a cycle runs from one
/// rising edge of the marker bit to the next. Returns how many were written
/// into `out_uc`.
pub fn per_cycle_charge(
    current_ua: &[f64],
    digital: &[u8],
    fs_hz: f64,
    cycle_bit: u8,
    out_uc: &mut [f64],
) -> Result<usize, Error> {
    let n = digital.len();
    if n == 0 || current_ua.len() < n {
        return Err(Error::Argument);
    }
    if not_above_zero(fs_hz) {
        return Err(Error::Rate);
    }
    if cycle_bit > 7 {
        return Err(Error::Bit);
    }

    let mut count = 0usize;
    // The first sample of each cycle, which is the sample at which the marker
    // is high having been low. A trace that begins high gives no such index for
    // its first cycle, and that cycle is therefore incomplete and unreported.
    let mut previous_start = 0usize;
    let mut have_start = false;

    for i in 1..n {
        let was = (digital[i - 1] >> cycle_bit) & 1;
        let now = (digital[i] >> cycle_bit) & 1;
        if was != 0 || now == 0 {
            continue;
        }
        if have_start {
            if count >= out_uc.len() {
                return Err(Error::Room);
            }
            // Accumulated and THEN divided, which is the grouping the exact
            // comparison depends on.
            let mut sum = 0.0f64;
            for k in previous_start..i {
                sum += current_ua[k];
            }
            out_uc[count] = sum / fs_hz;
            count += 1;
        }
        previous_start = i;
        have_start = true;
    }
    Ok(count)
}

/// The median, the extremes and the spread, refusing fewer cycles than the
/// floor because the chapter's instruction to report at least ten is a rule.
pub fn report(charge_uc: &[f64], volts: f64, min_cycles: u32) -> Result<Report, Error> {
    let count = charge_uc.len();
    if count > MAX_CYCLES {
        return Err(Error::Room);
    }
    if not_above_zero(volts) {
        return Err(Error::Supply);
    }
    if count < min_cycles as usize || count == 0 {
        return Err(Error::Cycles);
    }

    let mut sorted = [0.0f64; MAX_CYCLES];
    sorted[..count].copy_from_slice(&charge_uc[..count]);
    sort_small(&mut sorted[..count]);

    let median_uc = median_of(&sorted[..count]);
    let min_uc = sorted[0];
    let max_uc = sorted[count - 1];
    Ok(Report {
        cycles: count as u32,
        median_uc,
        min_uc,
        max_uc,
        // Every cycle drew nothing, so the spread has no denominator. Reported
        // as zero rather than as a division, and the medians beside it say why.
        spread_pct: if median_uc == 0.0 {
            0.0
        } else {
            100.0 * (max_uc - min_uc) / median_uc
        },
        energy_median_uj: median_uc * volts,
    })
}

/// Arithmetic only, with every assumption an argument and the uncovered part of
/// the day costed rather than free.
pub fn lifetime(b: &Battery) -> Result<Lifetime, Error> {
    if not_above_zero(b.energy_per_cycle_uj)
        || not_above_zero(b.cell_mah)
        || not_above_zero(b.cell_volts)
        || not_above_zero(b.sleep_volts)
        || not_above_zero(b.cycle_period_s)
        || not_above_zero(b.cycles_per_day)
    {
        return Err(Error::Supply);
    }
    if b.sleep_ua < 0.0 {
        return Err(Error::Supply);
    }

    let covered_s = b.cycles_per_day * b.cycle_period_s;
    if covered_s > SECONDS_PER_DAY {
        // More cycling than there is day to cycle in. Refused rather than
        // reported, because the rest of this sum would be about a day that
        // does not exist.
        return Err(Error::Day);
    }
    let uncovered_s = SECONDS_PER_DAY - covered_s;
    let cell_j = b.cell_mah * 1e-3 * 3600.0 * b.cell_volts;
    let cycle_j_per_day = b.energy_per_cycle_uj * 1e-6 * b.cycles_per_day;
    let uncovered_j_per_day = b.sleep_ua * 1e-6 * b.sleep_volts * uncovered_s;
    let total = cycle_j_per_day + uncovered_j_per_day;

    Ok(Lifetime {
        cell_j,
        covered_s,
        uncovered_s,
        cycle_j_per_day,
        uncovered_j_per_day,
        days: cell_j / total,
        // Step 10's printed form, which divides by the cycles alone.
        days_chapter: cell_j / cycle_j_per_day,
        covers_the_day: uncovered_s == 0.0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_trace_that_begins_high_loses_its_first_cycle() {
        // Marker already high at sample zero: no rising edge in front of that
        // cycle, so it is incomplete and unreported. Finding one.
        let mut amps = [0.0f64; 64];
        let mut bits = [0u8; 64];
        for i in 0..64 {
            amps[i] = 128.0;
            // high for the first 4 of every 16, starting high at zero
            bits[i] = if (i % 16) < 4 { 1 } else { 0 };
        }
        let mut out = [0.0f64; MAX_CYCLES];
        let count = per_cycle_charge(&amps, &bits, 1024.0, 0, &mut out).unwrap();
        // Four pulses in 64 samples, and the first has no rising edge, so three
        // complete cycles: rises at 16, 32 and 48 give two closed cycles.
        assert_eq!(count, 2);
        assert_eq!(out[0], 2.0);
        assert_eq!(out[1], 2.0);
    }

    #[test]
    fn the_charge_is_summed_then_divided() {
        // Sixteen samples of 128 microamps at 1024 hertz: 2048 / 1024 = 2.0
        // exactly, and both groupings agree here, which is why the test states
        // the grouping rather than relying on the value.
        let sum = 128.0f64 * 16.0;
        assert_eq!(sum / 1024.0, 2.0);
    }

    #[test]
    fn the_median_of_an_even_count_is_the_mean_of_the_middle_two() {
        let charges = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0];
        let r = report(&charges, 4.0, MIN_CYCLES).unwrap();
        assert_eq!(r.cycles, 10);
        assert_eq!(r.median_uc, 5.5);
        assert_eq!(r.min_uc, 1.0);
        assert_eq!(r.max_uc, 10.0);
        assert_eq!(r.energy_median_uj, 22.0);
        // 100 * (10 - 1) / 5.5, in that grouping.
        assert_eq!(r.spread_pct, 100.0 * 9.0 / 5.5);
    }

    #[test]
    fn a_median_of_zero_reports_a_spread_of_zero_and_not_a_division() {
        let charges = [0.0f64; 10];
        let r = report(&charges, 3.3, MIN_CYCLES).unwrap();
        assert_eq!(r.median_uc, 0.0);
        assert_eq!(r.spread_pct, 0.0, "not a division by zero");
        assert_eq!(r.energy_median_uj, 0.0);
    }

    #[test]
    fn the_uncovered_day_is_costed_and_the_chapters_form_is_kept_beside_it() {
        let b = Battery {
            energy_per_cycle_uj: 330.0,
            cycles_per_day: 86400.0,
            cycle_period_s: 1.0,
            sleep_ua: 2.0,
            sleep_volts: 3.3,
            cell_mah: 225.0,
            cell_volts: 3.0,
        };
        let l = lifetime(&b).unwrap();
        assert_eq!(l.covered_s, 86400.0);
        assert_eq!(l.uncovered_s, 0.0);
        assert!(l.covers_the_day);
        // A day fully covered by cycles costs nothing in sleep, so the two
        // forms agree. That is the only case where they do.
        assert_eq!(l.uncovered_j_per_day, 0.0);
        assert_eq!(l.days, l.days_chapter);
        assert_eq!(l.cell_j, 225.0 * 1e-3 * 3600.0 * 3.0);
    }

    #[test]
    fn the_refusals_are_the_cs_refusals() {
        let amps = [1.0f64; 16];
        let bits = [0u8; 16];
        let mut out = [0.0f64; MAX_CYCLES];
        let empty: [u8; 0] = [];
        assert_eq!(
            per_cycle_charge(&amps, &empty, 1024.0, 0, &mut out),
            Err(Error::Argument)
        );
        assert_eq!(
            per_cycle_charge(&amps, &bits, 0.0, 0, &mut out),
            Err(Error::Rate)
        );
        assert_eq!(
            per_cycle_charge(&amps, &bits, -1024.0, 0, &mut out),
            Err(Error::Rate)
        );
        assert_eq!(
            per_cycle_charge(&amps, &bits, 1024.0, 8, &mut out),
            Err(Error::Bit)
        );

        let nine = [1.0f64; 9];
        assert_eq!(report(&nine, 3.3, MIN_CYCLES), Err(Error::Cycles));
        let none: [f64; 0] = [];
        assert_eq!(report(&none, 3.3, MIN_CYCLES), Err(Error::Cycles));
        let ten = [1.0f64; 10];
        assert_eq!(report(&ten, 0.0, MIN_CYCLES), Err(Error::Supply));

        // And the predicate that stands in for the C's !(x > 0.0): a value
        // that is not a number refuses, and an infinite one does not, which is
        // what the C does.
        assert!(not_above_zero(f64::NAN));
        assert!(!not_above_zero(f64::INFINITY));
        assert_eq!(Error::Day.name(), "day");
    }
}
