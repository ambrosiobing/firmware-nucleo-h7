//! P20's latency histogram, its percentiles and the verdict against table
//! 20.4, against the same oracle as the C and the Python.
//!
//! The same arithmetic, in the same order, as `../../c/latstat.c`. The five
//! findings this project is about are written out in `../../c/latstat.h` and
//! are not repeated here: a percentile read from a histogram is a bin and not a
//! value, the truncation biases toward passing, the overflow counter changes a
//! percentile's type, the rank arithmetic overflows 32 bits before the counter
//! does, and the kernel reports free stack where the budget table wants used.
//!
//! # What this language changes, and it is one of the five
//!
//! Finding four is that the 99.9th percentile's rank is the ceiling of 999
//! times the count over 1000, and that a 32-bit product wraps for a count this
//! counter can hold. In the C that wrap is silent and always: the rank comes
//! out small, the reported percentile is far below the truth, and it passes a
//! budget row the right answer fails. Here the same expression in 32 bits
//! behaves three ways, and **the useful statement is the narrow one**:
//!
//! | | 999 times a count of four thousand million, in 32 bits |
//! |---|---|
//! | C, every build | 1680414720, silently |
//! | Rust, debug or test | panics, "attempt to multiply with overflow" |
//! | Rust, this workspace's release profile | 1680414720, silently |
//!
//! Cargo's dev profile takes `overflow-checks` from `debug-assertions`, which
//! is on, and the root manifest's `[profile.release]` sets `opt-level`, `lto`,
//! `codegen-units`, `panic` and `debug` and **not** `overflow-checks`, so
//! release leaves it off. So the honest claim is that Rust's arithmetic checks
//! would have caught this during development and not in the field, which is
//! smaller than "this cannot happen in Rust" and is the only claim the two
//! profiles support. **That is the second project in this volume to find that
//! shape**, after P17's subtraction on Tuesday 27 October 2026; one instance was
//! an anecdote and two are a property of the toolchain.
//!
//! A test below shows the wrong rank through `wrapping_mul`, which is profile
//! independent, and the right one through the 64-bit product this crate uses.
//!
//! # What else the language changes, which is almost nothing
//!
//! `Error::Argument` is the C's null pointer refusal and **is unreachable
//! here**, because a reference cannot be null. It is kept so that the six
//! refusal names mean the same thing in all four implementations and a filter
//! answer never has to be translated. `Result` carries a refusal instead of a
//! negative return code, and the histogram is an array in a struct exactly as
//! it is in the C.
//!
//! This crate is `no_std`: every quantity here is an integer, so nothing needs a
//! platform.

#![no_std]

/// Step 5's instrument: one bin per microsecond, and the chapter's number.
pub const BINS: usize = 1024;

/// Table 20.4's three latency rows, in microseconds, quoted and not chosen.
pub const BUDGET_MEDIAN: u32 = 100;
pub const BUDGET_P999: u32 = 250;
pub const BUDGET_MAX: u32 = 1000;

/// The same six refusals as the C, in the same order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The C's null pointer refusal, unreachable here and kept for the name.
    Argument,
    Rate,
    Full,
    Empty,
    Count,
    Stack,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Rate => "rate",
            Error::Full => "full",
            Error::Empty => "empty",
            Error::Count => "count",
            Error::Stack => "stack",
        }
    }
}

/// Three verdicts, and the third one is finding one.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Verdict {
    Pass,
    Fail,
    Undecided,
}

impl Verdict {
    /// The C's 0, 1 and 2, so one oracle field serves all four languages.
    pub fn code(self) -> u8 {
        match self {
            Verdict::Pass => 0,
            Verdict::Fail => 1,
            Verdict::Undecided => 2,
        }
    }
}

#[derive(Debug, Clone)]
pub struct Hist {
    pub bins: [u32; BINS],
    /// Samples at or above BINS microseconds.
    pub over: u32,
    /// Running maximum, which the histogram cannot hold.
    pub max_us: u32,
    /// Every sample, including the ones in over.
    pub count: u32,
}

impl Default for Hist {
    fn default() -> Self {
        Hist {
            bins: [0u32; BINS],
            over: 0,
            max_us: 0,
            count: 0,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Report {
    /// The bin, meaningful only when resolved.
    pub median_us: u32,
    pub p999_us: u32,
    /// False when the rank fell inside the overflow count.
    pub median_resolved: bool,
    pub p999_resolved: bool,
    pub max_us: u32,
    pub over: u32,
    pub count: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Budget {
    pub median_us: u32,
    pub p999_us: u32,
    pub max_us: u32,
}

impl Default for Budget {
    /// Table 20.4, which was written before any measurement.
    fn default() -> Self {
        Budget {
            median_us: BUDGET_MEDIAN,
            p999_us: BUDGET_P999,
            max_us: BUDGET_MAX,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Verdicts {
    pub median: Verdict,
    pub p999: Verdict,
    pub max: Verdict,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Stack {
    /// The budget less the kernel's free figure.
    pub used: u32,
    /// Used per thousand of the budget, truncated.
    pub permil: u32,
    /// True when used is at or below eighty percent.
    pub within: bool,
}

/// The ceiling of count times numer over denom, in 64 bits. Finding four, and
/// the test at the bottom shows what 32 bits would have done instead.
fn rank_of(count: u32, numer: u32, denom: u32) -> u32 {
    let product = u64::from(count) * u64::from(numer);
    let rank = (product + u64::from(denom) - 1) / u64::from(denom);
    rank as u32
}

impl Hist {
    pub fn new() -> Self {
        Self::default()
    }

    /// One sample. `cycles_per_us` scales the core's cycle counter, and the
    /// division TRUNCATES, which finding two is about.
    pub fn record(&mut self, cycles: u32, cycles_per_us: u32) -> Result<(), Error> {
        if cycles_per_us == 0 {
            return Err(Error::Rate);
        }
        // Refused before the sample is taken rather than after the counter
        // wraps. A wrapped count makes every rank below it wrong and nothing in
        // the printed result would show it.
        if self.count == u32::MAX {
            return Err(Error::Full);
        }

        let us = cycles / cycles_per_us;
        if (us as usize) < BINS {
            self.bins[us as usize] += 1;
        } else {
            self.over += 1;
        }
        if us > self.max_us {
            self.max_us = us;
        }
        self.count += 1;
        Ok(())
    }

    /// The bin holding the sample of the given rank, counting from one. `None`
    /// when the rank falls inside the overflow count, which is finding three.
    fn bin_at_rank(&self, rank: u32) -> Option<u32> {
        let mut seen: u64 = 0;
        for (k, n) in self.bins.iter().enumerate() {
            seen += u64::from(*n);
            if seen >= u64::from(rank) {
                return Some(k as u32);
            }
        }
        None
    }

    /// The median bin, the 99.9th percentile's bin, the maximum and the
    /// overflow. Each percentile carries a flag saying whether the histogram
    /// could answer.
    pub fn report(&self) -> Result<Report, Error> {
        // A report of zero microseconds would read as the fastest run ever
        // measured, so no samples is a refusal and not a figure.
        if self.count == 0 {
            return Err(Error::Empty);
        }

        // The count is checked against the histogram rather than trusted. One
        // that did not come from record gives ranks pointing at the wrong
        // samples, and nothing in the output would show it.
        let total: u64 =
            self.bins.iter().map(|n| u64::from(*n)).sum::<u64>() + u64::from(self.over);
        if total != u64::from(self.count) {
            return Err(Error::Count);
        }

        // The rank definition is PINNED, because the chapter does not state one
        // and two reasonable choices differ by one sample. The percentile is
        // the sample of rank ceil(p * n), counting from one.
        let median = self.bin_at_rank(rank_of(self.count, 1, 2));
        let p999 = self.bin_at_rank(rank_of(self.count, 999, 1000));
        Ok(Report {
            median_us: median.unwrap_or(0),
            p999_us: p999.unwrap_or(0),
            median_resolved: median.is_some(),
            p999_resolved: p999.is_some(),
            max_us: self.max_us,
            over: self.over,
            count: self.count,
        })
    }
}

/// Finding one: bin k covers the true values from k up to just under k plus
/// one, so only a bin entirely on one side of the budget decides anything.
fn decide_bin(bin: u32, budget: u32) -> Verdict {
    if bin + 1 <= budget {
        return Verdict::Pass;
    }
    if bin > budget {
        return Verdict::Fail;
    }
    // bin == budget: part of this bin meets the budget and part does not, and
    // the truncation in record means the true value is at or above the
    // recorded one.
    Verdict::Undecided
}

/// An unresolved percentile is a value at or above BINS microseconds, which is
/// everything the histogram knows. That still fails any budget below BINS,
/// which is where all three of table 20.4's rows sit.
fn decide_unresolved(budget: u32) -> Verdict {
    if BINS as u32 > budget {
        return Verdict::Fail;
    }
    Verdict::Undecided
}

/// The three budget rows judged.
pub fn verdict(r: &Report, b: &Budget) -> Verdicts {
    Verdicts {
        median: if r.median_resolved {
            decide_bin(r.median_us, b.median_us)
        } else {
            decide_unresolved(b.median_us)
        },
        p999: if r.p999_resolved {
            decide_bin(r.p999_us, b.p999_us)
        } else {
            decide_unresolved(b.p999_us)
        },
        // The maximum is a recorded value and not a bin, but it was truncated
        // by the same division, so it is judged the same way.
        max: decide_bin(r.max_us, b.max_us),
    }
}

/// One task's stack, from the budget and the kernel's smallest free figure.
pub fn stack(budget: u32, free: u32) -> Result<Stack, Error> {
    if budget == 0 {
        return Err(Error::Stack);
    }
    // Finding five: more free space than the whole budget is impossible, so it
    // means the two numbers came from different tasks. Refused, not clamped.
    if free > budget {
        return Err(Error::Stack);
    }
    let used = budget - free;
    Ok(Stack {
        used,
        permil: ((u64::from(used) * 1000) / u64::from(budget)) as u32,
        // Eighty percent as a comparison and not a division, so the rule is
        // exact at the boundary: used over budget at or below four fifths.
        within: u64::from(used) * 5 <= u64::from(budget) * 4,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(pairs: &[(u32, u32)], cycles_per_us: u32) -> Hist {
        let mut h = Hist::new();
        for (cycles, repeat) in pairs {
            for _ in 0..*repeat {
                h.record(*cycles, cycles_per_us).unwrap();
            }
        }
        h
    }

    #[test]
    fn a_thousand_releases_at_one_hundred_microseconds() {
        let h = run(&[(28000, 1000)], 280);
        let r = h.report().unwrap();
        assert_eq!(r.count, 1000);
        assert_eq!(r.median_us, 100);
        assert_eq!(r.p999_us, 100);
        assert_eq!(r.max_us, 100);
        assert_eq!(r.over, 0);
        assert!(r.median_resolved && r.p999_resolved);

        // And the budget row for the median is exactly 100, so the bin
        // straddles it and the verdict is undecided rather than a pass. That is
        // finding one.
        let v = verdict(&r, &Budget::default());
        assert_eq!(v.median, Verdict::Undecided);
        assert_eq!(v.p999, Verdict::Pass);
        assert_eq!(v.max, Verdict::Pass);
    }

    /// Finding two: the truncation makes a run that misses the budget report
    /// the same figures as one that meets it.
    #[test]
    fn two_different_runs_report_the_same_median() {
        let met = run(&[(28000, 1000)], 280).report().unwrap();
        let missed = run(&[(28252, 1000)], 280).report().unwrap();
        // 28252 over 280 is one hundred and nine tenths, truncated to 100.
        assert_eq!(met.median_us, missed.median_us);
        assert_eq!(28252_u32 / 280, 100);
        // Every sample of the second exceeds a budget of 100, and the report
        // cannot tell them apart, which is why the verdict refuses to pass.
        assert_eq!(
            verdict(&met, &Budget::default()).median,
            verdict(&missed, &Budget::default()).median
        );
    }

    /// Finding three: a percentile whose rank falls in the overflow count has
    /// no bin, and the top bin would be the one undetectable wrong answer.
    #[test]
    fn an_unresolved_percentile_is_not_the_top_bin() {
        let h = run(&[(14000, 998), (560000, 2)], 280);
        let r = h.report().unwrap();
        assert_eq!(r.over, 2);
        assert_eq!(r.max_us, 2000);
        assert!(r.median_resolved);
        assert!(!r.p999_resolved, "the rank is inside the overflow");
        // Still decided, because at or above 1024 fails a budget of 250.
        let v = verdict(&r, &Budget::default());
        assert_eq!(v.p999, Verdict::Fail);
        assert_eq!(v.max, Verdict::Fail);

        // One release in a thousand leaves the rank inside the bins, which is
        // the control: the refusal is about where the rank lands and not about
        // the run having any overflow at all.
        let fewer = run(&[(14000, 999), (560000, 1)], 280).report().unwrap();
        assert_eq!(fewer.over, 1);
        assert!(fewer.p999_resolved);
        assert_eq!(fewer.p999_us, 50);
    }

    /// Finding four, in the form this language changes. `wrapping_mul` rather
    /// than a plain multiply, because a plain one panics in test and wraps in
    /// this workspace's release profile, and the point is what the C does in
    /// every build.
    #[test]
    fn the_rank_product_needs_more_than_32_bits() {
        let count: u32 = 4_000_000_000;
        let wrapped = count.wrapping_mul(999);
        assert_eq!(wrapped, 1_680_414_720);
        let wrong_rank = (wrapped / 1000) + 1;
        let right_rank = rank_of(count, 999, 1000);
        assert_eq!(right_rank, 3_996_000_000);
        assert!(wrong_rank < right_rank / 2);

        // And the two ranks land in different bins of one histogram, which is
        // what makes it a wrong answer rather than a wrong intermediate.
        let mut h = Hist::new();
        h.bins[50] = 2_000_000_000;
        h.bins[900] = 2_000_000_000;
        h.count = count;
        let r = h.report().unwrap();
        assert_eq!(r.p999_us, 900);
        assert_eq!(h.bin_at_rank(wrong_rank), Some(50));
    }

    /// Finding five: the kernel reports free stack and the table wants used.
    #[test]
    fn the_kernel_reports_free_and_the_table_wants_used() {
        let s = stack(1024, 205).unwrap();
        assert_eq!(s.used, 819);
        assert_eq!(s.permil, 799);
        assert!(s.within, "799 per thousand is inside eighty percent");

        // The same number read as used instead of free, which is the error
        // that hides the problem rather than inventing one.
        let misread = stack(1024, 1024 - 205).unwrap();
        assert_eq!(misread.used, 205);
        assert_eq!(misread.permil, 200);

        // The boundary is exact, by comparison rather than division.
        assert!(stack(1000, 200).unwrap().within);
        assert!(!stack(1000, 199).unwrap().within);
    }

    #[test]
    fn the_refusals_are_the_cs_refusals() {
        let mut h = Hist::new();
        assert_eq!(h.record(1000, 0), Err(Error::Rate));
        assert_eq!(h.report(), Err(Error::Empty));

        let mut full = Hist::new();
        full.count = u32::MAX;
        assert_eq!(full.record(1000, 280), Err(Error::Full));

        let mut wrong = Hist::new();
        wrong.bins[50] = 10;
        wrong.max_us = 50;
        wrong.count = 11;
        assert_eq!(wrong.report(), Err(Error::Count));

        assert_eq!(stack(0, 0), Err(Error::Stack));
        assert_eq!(stack(1024, 1025), Err(Error::Stack));

        // Argument is unreachable here and kept for the name, so the only
        // thing to assert about it is the name itself.
        assert_eq!(Error::Argument.name(), "argument");
        h.record(28000, 280).unwrap();
        assert_eq!(h.count, 1);
    }
}
