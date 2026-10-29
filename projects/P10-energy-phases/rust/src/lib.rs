//! P10's per-phase charge ledger and its residual in Rust, against the same
//! oracle as the C and the Python.
//!
//! The same arithmetic, in the same order, as `../../c/phaseledger.c`. The file
//! `../../c/phaseledger.h` carries the reasoning, including the two that matter
//! most: the total is integrated with no reference to the markers at all,
//! because a residual computed between the phase sum and itself would always be
//! zero and would prove nothing; and the runs are cut by INDEX rather than by
//! time, which is the whole correction this project makes to the chapter's
//! sketch.
//!
//! # The residual is the acceptance test, and it is meant to be non-zero
//!
//! A run shorter than the floor is rejected, and the charge drawn inside it
//! stays in the total while leaving the split, so the residual is non-zero by
//! construction whenever anything was rejected. `rejected_samples` is what
//! makes that checkable rather than a guess. A reader who expects the residual
//! to be zero has mistaken a measurement for a checksum.
//!
//! # `f64::abs` is in std, so the magnitude is written out, for the second time
//!
//! This crate is `no_std`, and `f64::abs` lives in std for the same reason
//! `sqrt` and `log10` do. The magnitude is therefore a comparison:
//!
//! ```text
//! if v < 0.0 { -v } else { v }
//! ```
//!
//! which is what the C writes too, by choice rather than by need. **P12's crate
//! was the first to meet this**, where the obvious spelling of its charge
//! reconciliation needed `abs` and was written as two comparisons instead so the
//! crate could stay `no_std`. Two crates now say it, which makes it a property
//! of the split between `core` and `std` rather than an anecdote about one file,
//! and the list of what this costs on this part is in that crate's README.
//!
//! Every other operation here is `+`, `-`, `*`, `/` and a comparison on `f64`,
//! all of which are in core, so nothing in this crate needs a platform.

#![no_std]

/// The chapter's marker codes: 0 idle, 1 wake, 2 sense, 3 compute, 4 send,
/// 7 fault, and 5 and 6 unused. The unused codes are carried rather than
/// refused, because a trace containing one is a finding and the report should be
/// able to show it.
pub const CODES: usize = 8;

/// The same four refusals as the C, in the same order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// A null pointer in the C, and in this language only an empty trace.
    Argument,
    Interval,
    Floor,
    Code,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Interval => "interval",
            Error::Floor => "floor",
            Error::Code => "code",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Ledger {
    /// Charge in coulombs, by marker code.
    pub per_code: [f64; CODES],
    /// Integrated with no reference to the markers.
    pub total: f64,
    /// The sum of the kept runs.
    pub split: f64,
    /// The magnitude of total less split, over the magnitude of total.
    pub residual: f64,
    /// Runs kept.
    pub runs: u32,
    /// Runs dropped as shorter than the floor.
    pub rejected: u32,
    /// Samples inside those runs, which is where the residual comes from.
    pub rejected_samples: u32,
}

impl Default for Ledger {
    fn default() -> Self {
        Ledger {
            per_code: [0.0; CODES],
            total: 0.0,
            split: 0.0,
            residual: 0.0,
            runs: 0,
            rejected: 0,
            rejected_samples: 0,
        }
    }
}

/// `f64::abs` is in std, so this is the comparison the C writes as well.
fn magnitude(v: f64) -> f64 {
    if v < 0.0 {
        -v
    } else {
        v
    }
}

/// The C's `!(x > 0.0)`: zero, negatives and any value that is not a number are
/// refused, and an infinite one is accepted, which is what the C does. Written
/// this way because clippy rejects `!(x > 0.0)` as `neg_cmp_op_on_partial_ord`,
/// and for a value that is not a number the first term is true while for every
/// other value `x <= 0.0` is exactly the negation of `x > 0.0`.
fn not_above_zero(x: f64) -> bool {
    x.is_nan() || x <= 0.0
}

/// The ledger for one trace. `min_samples` is the shortest run that counts as a
/// phase: one keeps every run, and two rejects single-sample runs, which is what
/// an instrument sampling at the rate the markers change cannot tell from a
/// glitch.
pub fn phase_ledger(
    current_a: &[f64],
    codes: &[u8],
    dt_s: f64,
    min_samples: u32,
) -> Result<Ledger, Error> {
    let n = codes.len();
    if n == 0 || current_a.len() < n {
        return Err(Error::Argument);
    }
    if not_above_zero(dt_s) {
        return Err(Error::Interval);
    }
    if min_samples == 0 {
        return Err(Error::Floor);
    }
    // Every code checked before anything is integrated, so a refusal leaves no
    // half-filled answer behind, which is what the C's "having written nothing"
    // means.
    if codes.iter().any(|c| usize::from(*c) >= CODES) {
        return Err(Error::Code);
    }

    let mut out = Ledger::default();

    // The total, integrated FIRST and with no reference to the markers at all.
    // Accumulated and then scaled, which is the C's grouping and therefore part
    // of the answer: the oracle compares this field exactly.
    let mut total = 0.0f64;
    for v in &current_a[..n] {
        total += *v;
    }
    total *= dt_s;

    // The runs, by index rather than by time.
    let mut split = 0.0f64;
    let mut start = 0usize;
    for i in 1..=n {
        if i != n && codes[i] == codes[start] {
            continue;
        }
        let count = (i - start) as u32;
        if count >= min_samples {
            let mut q = 0.0f64;
            for v in &current_a[start..i] {
                q += *v;
            }
            q *= dt_s;
            out.per_code[usize::from(codes[start])] += q;
            split += q;
            out.runs += 1;
        } else {
            out.rejected += 1;
            out.rejected_samples += count;
        }
        start = i;
    }

    out.total = total;
    out.split = split;
    // A total of zero has no denominator, and the C answers one rather than
    // dividing: a residual of zero there would read as a perfect split of
    // nothing.
    out.residual = if total != 0.0 {
        magnitude(total - split) / magnitude(total)
    } else {
        1.0
    };
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The oracle's interval: two to the minus ten seconds, so every charge is
    /// a sum of powers of two scaled by a power of two, which is exact in f64,
    /// and nothing in this project passes on a tolerance.
    const DT: f64 = 0.0009765625;

    #[test]
    fn one_sample_of_one_code() {
        let got = phase_ledger(&[0.5], &[0], DT, 1).unwrap();
        assert_eq!(got.total, 0.00048828125);
        assert_eq!(got.per_code[0], 0.00048828125);
        assert_eq!(got.split, got.total);
        assert_eq!(got.residual, 0.0);
        assert_eq!(got.runs, 1);
    }

    #[test]
    fn runs_are_cut_by_index_and_a_code_can_recur() {
        // 1 1 2 2 1 1: three runs, and code 1 appears in two of them, so its
        // per-code charge is the sum of both rather than the last.
        let amps = [1.0f64; 6];
        let codes = [1u8, 1, 2, 2, 1, 1];
        let got = phase_ledger(&amps, &codes, DT, 1).unwrap();
        assert_eq!(got.runs, 3);
        assert_eq!(got.per_code[1], 4.0 * DT);
        assert_eq!(got.per_code[2], 2.0 * DT);
        assert_eq!(got.residual, 0.0);
    }

    /// The residual is MEANT to be non-zero here, which is the project's point.
    #[test]
    fn a_rejected_run_leaves_its_charge_in_the_total_and_not_the_split() {
        // A single sample of code 2 between two runs of code 1, with a floor of
        // two: that run is rejected, its charge stays in the total, and the
        // residual is what shows it.
        let amps = [1.0f64; 5];
        let codes = [1u8, 1, 2, 1, 1];
        let got = phase_ledger(&amps, &codes, DT, 2).unwrap();
        assert_eq!(got.runs, 2);
        assert_eq!(got.rejected, 1);
        assert_eq!(got.rejected_samples, 1);
        assert_eq!(got.per_code[2], 0.0, "nothing was attributed to it");
        assert_eq!(got.total, 5.0 * DT);
        assert_eq!(got.split, 4.0 * DT);
        // One sample of five is missing from the split.
        assert_eq!(got.residual, (1.0 * DT) / (5.0 * DT));
    }

    #[test]
    fn a_total_of_zero_answers_one_rather_than_dividing() {
        let got = phase_ledger(&[0.0, 0.0], &[0, 0], DT, 1).unwrap();
        assert_eq!(got.total, 0.0);
        assert_eq!(got.residual, 1.0, "not zero, which would read as perfect");
    }

    #[test]
    fn a_negative_current_is_integrated_and_the_magnitude_is_written_out() {
        // Charge can be negative if the instrument reads below its zero, and
        // the residual takes magnitudes so it stays positive either way.
        let got = phase_ledger(&[-1.0, -1.0], &[3, 3], DT, 1).unwrap();
        assert_eq!(got.total, -2.0 * DT);
        assert_eq!(got.residual, 0.0);
        assert_eq!(magnitude(-2.0 * DT), 2.0 * DT);
        assert_eq!(magnitude(0.0), 0.0);
    }

    #[test]
    fn the_refusals_are_the_cs_refusals() {
        let amps = [1.0f64; 4];
        let codes = [0u8; 4];
        let empty: [u8; 0] = [];
        assert_eq!(phase_ledger(&amps, &empty, DT, 1), Err(Error::Argument));
        assert_eq!(phase_ledger(&amps, &codes, 0.0, 1), Err(Error::Interval));
        assert_eq!(phase_ledger(&amps, &codes, -DT, 1), Err(Error::Interval));
        assert_eq!(phase_ledger(&amps, &codes, DT, 0), Err(Error::Floor));
        let high = [0u8, 1, 8, 2];
        assert_eq!(phase_ledger(&amps, &high, DT, 1), Err(Error::Code));

        // The predicate that stands in for the C's !(x > 0.0).
        assert!(not_above_zero(f64::NAN));
        assert!(!not_above_zero(f64::INFINITY));
    }
}
