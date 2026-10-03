//! P03's loss attribution in Rust: every sent byte to exactly one place.
//!
//! The same arithmetic and the same verdict order as `../c/attribute.c`,
//! written independently. This is the half of P03 that can be proven: the
//! receive path needs a peripheral and refuses until RM0455 is read, while this
//! is arithmetic over counters a run recorded, and it is the half where a wrong
//! answer publishes a wrong conclusion rather than merely failing to work.
//!
//! **Why it is worth four implementations.** The easiest way to publish a wrong
//! result about a serial link is to report one figure called "bytes lost". The
//! common finding, that a link fails above some rate, is usually a statement
//! about the bridge between the host and the board and not about the board. A
//! number that cannot tell those apart is worse than no number, because
//! somebody will quote it.
//!
//! **The order of the verdicts is load bearing.** Bridge is tested first, so a
//! step that lost bytes before the peripheral saw them is never also reported as
//! a target finding.
//!
//! **What Rust adds, and here it is genuinely the most of the four.** Two
//! things, and both change the shape rather than the wording.
//!
//! The refusal is a `Result`, so a caller cannot read a row that was never
//! produced. The C returns `-1` and fills nothing, and a caller that forgot to
//! check reads uninitialised fields; the C++ returns `Option` and is as safe as
//! this.
//!
//! The second is the one the C cannot have at all: `Verdict` is an enum with
//! five variants and `match` on it is exhaustive, so adding a sixth kind of loss
//! later will not compile until every place that decides what a verdict means
//! has been visited. In the C a sixth enumerator compiles everywhere and
//! silently falls through whatever `else` was written last. For a file whose
//! entire purpose is that two kinds of loss are never confused, that is the
//! property worth having.

#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]

/// One byte in a million, as a reciprocal so no floating point is involved.
///
/// The Python twin computes the same tolerance as `int(sent * 1e-6)`, and the
/// two were checked for divergence rather than assumed equal. `1e-6` is slightly
/// below one millionth, so the product is slightly below the true quotient, but
/// the multiply rounds back onto the exact integer: the relative error is about
/// 2e-17 and the spacing of doubles near the largest possible quotient, about
/// 4295, is about 1e-12. They agree for every `sent` in the 32-bit range, which
/// was swept rather than argued. The integer form is kept here because it is
/// what a target would compute, not because the Python is wrong.
pub const BRIDGE_TOLERANCE_RECIPROCAL: u32 = 1_000_000;

/// What a step was. The order of the variants is the order the verdicts are
/// decided in, which is not an accident.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Verdict {
    /// The target lost nothing.
    Pass,
    /// Bytes went missing before the peripheral saw them, so this step says
    /// nothing about the target.
    Bridge,
    /// Overruns only: the handler was too late. A larger ring will not help.
    Latency,
    /// Drops only: the consumer could not keep up. A shorter handler will not
    /// help.
    Throughput,
    /// Both kinds, which are still not summed.
    Both,
}

impl Verdict {
    /// The name the other three implementations print, so one vocabulary serves
    /// all four.
    pub fn name(self) -> &'static str {
        match self {
            Verdict::Pass => "PASS",
            Verdict::Bridge => "BRIDGE",
            Verdict::Latency => "LATENCY",
            Verdict::Throughput => "THROUGHPUT",
            Verdict::Both => "BOTH",
        }
    }

    /// Whether this verdict is the target losing a byte. Written once here
    /// rather than as a condition at each use, because the whole point is that
    /// Pass and Bridge are not target loss and a reader should not have to
    /// check that three times.
    pub fn is_target_loss(self) -> bool {
        matches!(self, Verdict::Latency | Verdict::Throughput | Verdict::Both)
    }
}

/// Why an attribution was refused.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Refused {
    /// The target accounted for more bytes than the host sent. That is a defect
    /// in the measurement rather than a finding about the link: a counter that
    /// was not reset between steps, or a wrong host count. Reporting a negative
    /// bridge loss, or clamping it to zero, would turn the defect into a
    /// plausible-looking row.
    OverAccounted { reached: u64, sent: u32 },
}

/// What a run recorded for one step.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Step {
    pub rate: u32,
    pub sent: u32,
    pub accepted: u32,
    pub overruns: u32,
    pub dropped: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Row {
    pub rate: u32,
    pub sent: u32,
    pub delivered: u32,
    pub overrun: u32,
    pub dropped: u32,
    pub bridge_lost: u32,
    pub target_lost: u32,
    pub reached: u32,
    pub verdict: Verdict,
}

/// Attribute one step.
pub fn attribute(step: &Step) -> Result<Row, Refused> {
    let sent = step.sent;
    let delivered = step.accepted;
    let overrun = step.overruns;
    let dropped = step.dropped;

    // Summed in 64 bits, as the C and the C++ do and for the same reason: three
    // 32-bit counters can exceed a 32-bit sum between them, and a wrapped sum
    // would make `reached` smaller than it is and turn an over-accounted step
    // into a plausible bridge loss. In Rust a wrapping add would need asking
    // for, so the debug build would have panicked instead; widening is still
    // the right answer, because the comparison is with `sent` and not with a
    // 32-bit truth.
    let reached = u64::from(delivered) + u64::from(overrun) + u64::from(dropped);

    if reached > u64::from(sent) {
        return Err(Refused::OverAccounted { reached, sent });
    }

    let bridge_lost = (u64::from(sent) - reached) as u32;
    let target_lost = overrun + dropped;

    // One byte in a million, and at least one, so a short run is not judged by a
    // tolerance that rounds to zero.
    let tolerance = (sent / BRIDGE_TOLERANCE_RECIPROCAL).max(1);

    let verdict = if bridge_lost > tolerance {
        Verdict::Bridge
    } else if target_lost == 0 {
        Verdict::Pass
    } else if overrun > 0 && dropped == 0 {
        Verdict::Latency
    } else if dropped > 0 && overrun == 0 {
        Verdict::Throughput
    } else {
        Verdict::Both
    };

    Ok(Row {
        rate: step.rate,
        sent,
        delivered,
        overrun,
        dropped,
        bridge_lost,
        target_lost,
        reached: reached as u32,
        verdict,
    })
}

/// The index of the lowest-rate row where the target itself lost a byte, or
/// `None` when none did.
///
/// Bridge rows are skipped rather than counted as a target failure, because that
/// is exactly the confusion this file exists to prevent. A ramp whose every row
/// is Bridge has found nothing about the target, and `None` says so rather than
/// naming the lowest bridge failure.
pub fn first_loss(rows: &[Row]) -> Option<usize> {
    rows.iter()
        .enumerate()
        .filter(|(_, r)| r.verdict.is_target_loss())
        .min_by_key(|(_, r)| r.rate)
        .map(|(i, _)| i)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn step(rate: u32, sent: u32, accepted: u32, overruns: u32, dropped: u32) -> Step {
        Step {
            rate,
            sent,
            accepted,
            overruns,
            dropped,
        }
    }

    /// The synthetic cases are deliberately NOT all here. They live in
    /// `python/tests/test_attribute_parity.py`, which drives all four
    /// implementations over one list. What belongs here is the properties that
    /// need no other implementation, and the two the compiler can be asked.
    #[test]
    fn a_clean_step_is_a_pass_and_blames_nobody() {
        let r = attribute(&step(115_200, 1_000_000, 1_000_000, 0, 0)).unwrap();
        assert_eq!(r.verdict, Verdict::Pass);
        assert_eq!(r.bridge_lost, 0);
        assert_eq!(r.target_lost, 0);
    }

    #[test]
    fn the_two_kinds_of_target_loss_are_never_summed() {
        let latency = attribute(&step(460_800, 1_000_000, 999_000, 1_000, 0)).unwrap();
        assert_eq!(latency.verdict, Verdict::Latency);
        let throughput = attribute(&step(460_800, 1_000_000, 999_000, 0, 1_000)).unwrap();
        assert_eq!(throughput.verdict, Verdict::Throughput);
        let both = attribute(&step(921_600, 1_000_000, 998_000, 1_000, 1_000)).unwrap();
        assert_eq!(both.verdict, Verdict::Both);
        // The same total loss, three different findings and three different
        // fixes. A single figure called "lost" would be 1000 in all three.
        assert_eq!(latency.target_lost, 1_000);
        assert_eq!(throughput.target_lost, 1_000);
        assert_eq!(both.target_lost, 2_000);
    }

    #[test]
    fn bytes_missing_before_the_peripheral_are_the_bridge_and_not_the_target() {
        let r = attribute(&step(921_600, 1_000_000, 900_000, 0, 0)).unwrap();
        assert_eq!(r.verdict, Verdict::Bridge);
        assert_eq!(r.bridge_lost, 100_000);
        assert_eq!(r.target_lost, 0, "the target must not be blamed for the bridge");
    }

    #[test]
    fn a_handful_astray_in_millions_is_not_a_finding() {
        let r = attribute(&step(115_200, 10_000_000, 10_000_000 - 3, 0, 0)).unwrap();
        assert_eq!(r.verdict, Verdict::Pass);
    }

    #[test]
    fn over_accounting_is_refused_and_says_by_how_much() {
        let e = attribute(&step(115_200, 1_000, 1_200, 0, 0)).unwrap_err();
        assert_eq!(
            e,
            Refused::OverAccounted {
                reached: 1_200,
                sent: 1_000
            }
        );
    }

    #[test]
    fn three_counters_that_exceed_a_32_bit_sum_are_refused_rather_than_wrapped() {
        // Each counter is under 2^32 and their sum is not. A 32-bit sum would
        // wrap to something small and report a plausible bridge loss, which is
        // the defect the 64-bit widening exists to prevent.
        let e = attribute(&step(1, u32::MAX, u32::MAX, u32::MAX, u32::MAX)).unwrap_err();
        assert!(matches!(e, Refused::OverAccounted { .. }));
    }

    #[test]
    fn first_loss_takes_the_lowest_rate_and_skips_bridge_rows() {
        let rows: [Row; 4] = [
            attribute(&step(115_200, 1_000_000, 1_000_000, 0, 0)).unwrap(),
            attribute(&step(230_400, 1_000_000, 1_000_000, 0, 0)).unwrap(),
            attribute(&step(460_800, 1_000_000, 999_500, 500, 0)).unwrap(),
            attribute(&step(921_600, 1_000_000, 500_000, 0, 0)).unwrap(),
        ];
        let at = first_loss(&rows).expect("there is a target finding");
        assert_eq!(rows[at].rate, 460_800);
        assert_eq!(rows[at].verdict, Verdict::Latency);
        // The 921600 row is a bridge failure at a higher rate. Counting it
        // would report the bridge's limit as the target's.
        assert_eq!(rows[3].verdict, Verdict::Bridge);
    }

    #[test]
    fn a_ramp_that_only_measured_the_bridge_reports_no_target_finding() {
        let rows: [Row; 2] = [
            attribute(&step(115_200, 1_000_000, 800_000, 0, 0)).unwrap(),
            attribute(&step(230_400, 1_000_000, 700_000, 0, 0)).unwrap(),
        ];
        assert!(rows.iter().all(|r| r.verdict == Verdict::Bridge));
        assert_eq!(first_loss(&rows), None, "None is the honest answer");
    }

    #[test]
    fn the_rows_need_not_be_sorted() {
        // The lowest rate is found, not the first row, which the C's loop and
        // this one both do and which a naive "return the first match" would
        // get wrong on an unsorted ramp.
        let rows: [Row; 2] = [
            attribute(&step(921_600, 1_000_000, 999_000, 1_000, 0)).unwrap(),
            attribute(&step(230_400, 1_000_000, 999_000, 1_000, 0)).unwrap(),
        ];
        assert_eq!(first_loss(&rows), Some(1));
    }
}
