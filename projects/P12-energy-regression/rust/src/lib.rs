//! P12's two gates in Rust: a measurement, a committed expectation, a tolerance,
//! and a verdict that must be able to fail.
//!
//! The same rules, in the same order, as `../../c/gates.c`,
//! `../../cpp/gates.hpp` and `../../python/gates.py`. `../../PROTOCOL.md` is the
//! contract all four obey, and it says why the order of the charge gate's four
//! refusals is part of the rules rather than an accident of how the code reads.
//!
//! # `no_std` with `alloc`, and that is the finding
//!
//! P06's rate witness is the one crate in this workspace that is not `no_std`,
//! because `f64::sqrt` lives in `std` and not in `core`: Rust's core library has
//! no floating-point maths at all, since those functions live in the platform's
//! libm and `core` assumes no platform. A `no_std` P06 would need the `libm`
//! crate.
//!
//! This gate came within one function call of the same fate. It needs the
//! absolute difference between a ledger's reported total and the sum of its
//! phases, and `f64::abs` is in `std` for exactly the same reason `sqrt` is. One
//! dependency, for one call.
//!
//! It is written as two comparisons instead, at `charge_answer` below, and the
//! line says so where it happens. So this crate is `no_std`, needs nothing, and
//! `cargo build --target thumbv7em-none-eabihf --lib` includes it where it
//! excludes P06.
//!
//! **What it does need is `alloc`.** The answer is a string of unknown length
//! built from a map of unknown size, and that is allocation however it is
//! spelled. The C avoids it with fixed buffers and a refusal when they do not
//! fit, which is the right shape for a file that compiles for the target, and it
//! costs the C about sixty lines of appending and bounds checking that this file
//! does not have. Neither is better: they are the same rules written for
//! different machines, and the project README says which is which rather than
//! implying the four implementations are interchangeable everywhere.
//!
//! A `--lib` build for the board's target needs no allocator, which is why the
//! CI step can include this crate. A binary would need a `#[global_allocator]`,
//! and nothing in this repository has one.

#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]

extern crate alloc;

use alloc::collections::BTreeMap;
use alloc::format;
use alloc::string::{String, ToString};
use alloc::vec::Vec;

/// The chapter's figure: a five percent charge regression must turn a build red.
pub const CHARGE_TOLERANCE_DEFAULT: f64 = 0.05;

/// One target's reported or budgeted sizes. `None` is a field that was not
/// reported at all, which is a different case from one reported as zero, and the
/// gate treats them differently.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Sizes {
    pub flash: Option<i64>,
    pub static_ram: Option<i64>,
}

/// A `BTreeMap` and not a `HashMap`, because the rules are stated in name order:
/// the gate reports flash before static_ram within a target, and targets in name
/// order, so that two implementations handed the same case in different orders
/// still produce the same answer. The Python sorts its dictionary keys and the C
/// sorts an array of pointers; here the container does it.
pub type SizeMap = BTreeMap<String, Sizes>;
pub type PhaseMap = BTreeMap<String, f64>;

/// A double in the answer, in the shortest form that round-trips exactly.
///
/// `../../PROTOCOL.md` allows each language its own spelling here and says why.
/// The C and the C++ write `%.17g`; Rust has no such format, and `{:?}` is its
/// shortest round-tripping form. The test reads both back with Python's
/// `float()`, so the comparison is between the doubles the three computed and
/// not between the strings they chose. Writing a `%.17g` imitation in Rust to
/// make the strings match would have put a float formatter of my own in a file
/// whose job is agreeing with three other implementations.
fn g(value: f64) -> String {
    format!("{:?}", value)
}

/// The canonical answer of `../../PROTOCOL.md`, assembled from its four fields so
/// no caller has to remember the order or the separators.
fn answer(failed: bool, refusal: &str, failures: &[String], report: &[String]) -> String {
    let join = |items: &[String]| -> String {
        if items.is_empty() {
            "-".to_string()
        } else {
            items.join(",")
        }
    };
    format!(
        "{} {} {} {}",
        if failed { "FAIL" } else { "PASS" },
        if refusal.is_empty() { "-" } else { refusal },
        join(failures),
        join(report)
    )
}

// --------------------------------------------------------------- the size gate

/// The size gate: reported sizes against committed budgets.
pub fn size_answer(measured: &SizeMap, budgets: &SizeMap) -> String {
    // A gate with no input at all has nothing to report, and says so rather than
    // passing. A build that produced no sizes is a build failure wearing a
    // passing gate's clothes.
    if measured.is_empty() {
        return answer(true, "no_sizes", &[], &[]);
    }

    let mut failures: Vec<String> = Vec::new();
    let mut report: Vec<String> = Vec::new();

    for (target, got) in measured {
        let want = match budgets.get(target) {
            Some(w) => w,
            None => {
                failures.push(format!("no_budget:{}", target));
                continue;
            }
        };

        // flash first and then static_ram, which is the order the other three
        // produce and therefore the order of the failures a reader sees.
        for (field, used_opt, limit_opt) in [
            ("flash", got.flash, want.flash),
            ("static_ram", got.static_ram, want.static_ram),
        ] {
            let used = match used_opt {
                Some(u) => u,
                None => {
                    failures.push(format!("no_field:{}:{}", target, field));
                    continue;
                }
            };
            let limit = match limit_opt {
                Some(l) => l,
                None => {
                    failures.push(format!("no_field_budget:{}:{}", target, field));
                    continue;
                }
            };
            let pct = if limit != 0 {
                100.0 * (used as f64) / (limit as f64)
            } else {
                0.0
            };
            report.push(format!(
                "{}:{}:{}:{}:{}:{}",
                target,
                field,
                used,
                limit,
                g(pct),
                limit - used
            ));
            if used > limit {
                failures.push(format!(
                    "over:{}:{}:{}:{}:{}",
                    target,
                    field,
                    used,
                    limit,
                    used - limit
                ));
            }
        }
    }

    answer(!failures.is_empty(), "", &failures, &report)
}

// ------------------------------------------------------------- the charge gate

/// The charge gate: a measured ledger against the committed baseline.
///
/// `total_uc` of `None` is a ledger that carries no total at all, which is
/// refused rather than treated as zero.
pub fn charge_answer(
    build: &str,
    phases: &PhaseMap,
    total_uc: Option<f64>,
    baseline: &PhaseMap,
    tolerance: f64,
) -> String {
    // The four refusals in the order PROTOCOL.md fixes. Build identity first: a
    // capture that cannot be tied to a firmware is not evidence whatever its
    // numbers say, so there is no point examining them.
    if build.is_empty() {
        return answer(true, "no_build", &[], &[]);
    }
    if phases.is_empty() || baseline.is_empty() {
        return answer(true, "no_phases", &[], &[]);
    }
    let total = match total_uc {
        Some(t) => t,
        None => return answer(true, "no_total", &[], &[]),
    };

    // Summed in name order, because a BTreeMap iterates in name order and the
    // other three sort before summing. Addition is not associative in floating
    // point, and this is the one sum whose order is visible in the answer.
    let mut summed = 0.0;
    for value in phases.values() {
        summed += value;
    }

    // A run whose parts do not reconcile has lost or double counted a phase, and
    // no verdict on it is worth anything. Before any phase is compared, for
    // exactly that reason.
    //
    // THIS IS THE LINE THAT KEEPS THIS CRATE no_std. The obvious spelling is
    // (summed - total).abs() > slack, and f64::abs lives in std and not in core
    // for the same reason f64::sqrt does, so that one call would have cost this
    // crate the libm dependency. Two comparisons need nothing, and they behave
    // identically on a NaN: abs(NaN) > slack is false, and so are both of these.
    let slack = if 0.05 * total > 1.0 { 0.05 * total } else { 1.0 };
    if summed - total > slack || total - summed > slack {
        return answer(
            true,
            &format!("sum_mismatch:{}:{}", g(summed), g(total)),
            &[],
            &[],
        );
    }

    let mut failures: Vec<String> = Vec::new();
    let mut report: Vec<String> = Vec::new();

    // The baseline's phases first. A phase the baseline watches and the ledger
    // does not report is a failure and not a pass: losing a phase looks like an
    // improvement, which is the direction that flatters the work.
    for (name, want) in baseline {
        // Not named `g`: that is the double formatter above, and shadowing it
        // here would compile and would make the three calls below read wrongly.
        let got = match phases.get(name) {
            Some(value) => *value,
            None => {
                failures.push(format!("missing_phase:{}", name));
                continue;
            }
        };
        let want = *want;
        let change = if want != 0.0 {
            100.0 * (got - want) / want
        } else {
            0.0
        };
        report.push(format!("{}:{}:{}:{}", name, g(got), g(want), g(change)));
        if got > want * (1.0 + tolerance) {
            failures.push(format!("over:{}:{}:{}:{}", name, g(got), g(want), g(change)));
        }
    }

    // Then the measured phases. One the baseline does not carry is a failure, or
    // the gate would be watching less than the run does.
    for name in phases.keys() {
        if !baseline.contains_key(name) {
            failures.push(format!("not_in_baseline:{}", name));
        }
    }

    answer(!failures.is_empty(), "", &failures, &report)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sizes(flash: Option<i64>, ram: Option<i64>) -> Sizes {
        Sizes {
            flash,
            static_ram: ram,
        }
    }

    fn baseline() -> PhaseMap {
        let mut m = PhaseMap::new();
        m.insert("wake".to_string(), 100.0);
        m.insert("sense".to_string(), 500.0);
        m.insert("compute".to_string(), 100.0);
        m.insert("send".to_string(), 600.0);
        m.insert("sleep".to_string(), 20.0);
        m
    }

    fn ledger_from(base: &PhaseMap) -> PhaseMap {
        base.clone()
    }

    fn total_of(m: &PhaseMap) -> f64 {
        m.values().sum()
    }

    /// The chapter's acceptance criterion, asserted here rather than described.
    #[test]
    fn a_six_percent_charge_regression_turns_the_build_red() {
        let base = baseline();
        let mut run = ledger_from(&base);
        run.insert("send".to_string(), 600.0 * 1.06);
        let out = charge_answer(
            "v0.1-3-gdeadbee",
            &run,
            Some(total_of(&run)),
            &base,
            CHARGE_TOLERANCE_DEFAULT,
        );
        assert!(out.starts_with("FAIL "), "{}", out);
        assert!(out.contains("over:send:"), "{}", out);
    }

    /// The other half of the claim. A gate that failed everything would also
    /// satisfy the test above, so the boundary is pinned from both sides.
    #[test]
    fn a_regression_just_inside_the_tolerance_passes() {
        let base = baseline();
        let mut run = ledger_from(&base);
        run.insert(
            "send".to_string(),
            600.0 * (1.0 + CHARGE_TOLERANCE_DEFAULT * 0.9),
        );
        let out = charge_answer(
            "v0.1",
            &run,
            Some(total_of(&run)),
            &base,
            CHARGE_TOLERANCE_DEFAULT,
        );
        assert!(out.starts_with("PASS "), "{}", out);
    }

    /// Using less charge than the baseline is the point of the work.
    #[test]
    fn an_improvement_is_not_a_failure() {
        let base = baseline();
        let mut run = ledger_from(&base);
        run.insert("send".to_string(), 400.0);
        let out = charge_answer(
            "v0.1",
            &run,
            Some(total_of(&run)),
            &base,
            CHARGE_TOLERANCE_DEFAULT,
        );
        assert!(out.starts_with("PASS "), "{}", out);
    }

    /// Losing a phase looks like an improvement: fewer phases, less total
    /// charge, everything green. The single most dangerous way for an energy
    /// gate to be wrong, because the direction of the error flatters the work.
    #[test]
    fn a_missing_phase_is_a_failure_and_not_an_improvement() {
        let base = baseline();
        let mut run = ledger_from(&base);
        run.remove("send");
        let out = charge_answer(
            "v0.1",
            &run,
            Some(total_of(&run)),
            &base,
            CHARGE_TOLERANCE_DEFAULT,
        );
        assert!(out.contains("missing_phase:send"), "{}", out);
    }

    /// The four refusals are ordered, and the order is part of the rules. A
    /// ledger with no build identity and a broken sum is refused for the
    /// identity, because a capture that cannot be tied to a firmware is not
    /// evidence whatever its numbers say.
    #[test]
    fn the_build_identity_is_refused_before_the_arithmetic_is_examined() {
        let base = baseline();
        let mut run = ledger_from(&base);
        run.insert("send".to_string(), 99999.0);
        let out = charge_answer("", &run, Some(1.0), &base, CHARGE_TOLERANCE_DEFAULT);
        assert_eq!(out, "FAIL no_build - -", "{}", out);
    }

    #[test]
    fn phases_that_do_not_sum_to_the_total_are_refused() {
        let base = baseline();
        let run = ledger_from(&base);
        let out = charge_answer(
            "v0.1",
            &run,
            Some(total_of(&run) * 2.0),
            &base,
            CHARGE_TOLERANCE_DEFAULT,
        );
        assert!(out.contains("FAIL sum_mismatch:"), "{}", out);
    }

    #[test]
    fn a_phase_measured_but_not_in_the_baseline_is_a_failure() {
        let base = baseline();
        let mut run = ledger_from(&base);
        run.insert("radio".to_string(), 900.0);
        let out = charge_answer(
            "v0.1",
            &run,
            Some(total_of(&run)),
            &base,
            CHARGE_TOLERANCE_DEFAULT,
        );
        assert!(out.contains("not_in_baseline:radio"), "{}", out);
    }

    #[test]
    fn no_sizes_reported_at_all_is_a_failure_not_a_pass() {
        let mut budgets = SizeMap::new();
        budgets.insert("p01-first-light".to_string(), sizes(Some(16384), Some(12288)));
        assert_eq!(size_answer(&SizeMap::new(), &budgets), "FAIL no_sizes - -");
    }

    #[test]
    fn flash_over_budget_fails_and_says_by_how_much() {
        let mut budgets = SizeMap::new();
        budgets.insert("p01-first-light".to_string(), sizes(Some(16384), Some(12288)));
        let mut measured = SizeMap::new();
        measured.insert("p01-first-light".to_string(), sizes(Some(20000), Some(4000)));
        let out = size_answer(&measured, &budgets);
        assert!(
            out.contains("over:p01-first-light:flash:20000:16384:3616"),
            "{}",
            out
        );
    }

    /// A new binary nobody is watching the size of is how the margin disappears.
    #[test]
    fn a_target_with_no_committed_budget_is_a_failure() {
        let mut budgets = SizeMap::new();
        budgets.insert("p01-first-light".to_string(), sizes(Some(16384), Some(12288)));
        let mut measured = SizeMap::new();
        measured.insert("p99-new-thing".to_string(), sizes(Some(100), Some(100)));
        let out = size_answer(&measured, &budgets);
        assert!(out.contains("no_budget:p99-new-thing"), "{}", out);
    }

    /// A field the build did not report is not a field reported as zero, and the
    /// gate must not read the first as the second.
    #[test]
    fn a_build_that_omits_a_field_is_a_failure_and_not_a_zero() {
        let mut budgets = SizeMap::new();
        budgets.insert("p01-first-light".to_string(), sizes(Some(16384), Some(12288)));
        let mut measured = SizeMap::new();
        measured.insert("p01-first-light".to_string(), sizes(Some(9000), None));
        let out = size_answer(&measured, &budgets);
        assert!(
            out.contains("no_field:p01-first-light:static_ram"),
            "{}",
            out
        );
        assert!(!out.contains("static_ram:0:"), "{}", out);
    }

    /// The answer does not depend on the order the case was handed over, which
    /// is what the name ordering in the rules exists for.
    #[test]
    fn the_order_the_targets_arrive_in_does_not_change_the_answer() {
        let mut budgets = SizeMap::new();
        budgets.insert("alpha".to_string(), sizes(Some(100), Some(100)));
        budgets.insert("beta".to_string(), sizes(Some(100), Some(100)));

        let mut one = SizeMap::new();
        one.insert("beta".to_string(), sizes(Some(10), Some(10)));
        one.insert("alpha".to_string(), sizes(Some(20), Some(20)));

        let mut two = SizeMap::new();
        two.insert("alpha".to_string(), sizes(Some(20), Some(20)));
        two.insert("beta".to_string(), sizes(Some(10), Some(10)));

        assert_eq!(size_answer(&one, &budgets), size_answer(&two, &budgets));
    }
}
