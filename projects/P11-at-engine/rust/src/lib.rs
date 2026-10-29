//! P11's line classifier, deadline test and backoff, `no_std`.
//!
//! The same three pieces as `../../c/atparse.c` and `../../python/atparse.py`,
//! against the same oracle. The reasoning is in
//! [`../../c/atparse.h`](../../c/atparse.h) and is not repeated here, including
//! the one that decides the project: chapter 11's deadline test is a signed
//! difference on a 32 bit counter, the obvious form is correct for forty-nine
//! days and then is not, and **a board cannot test that without waiting
//! forty-nine days**, which is why this half of the chapter was written before
//! the modem arrived.
//!
//! THE SUBTRACTION IS `wrapping_sub` AND NOT `-`, WHICH IS THE FOURTH TIME THIS
//! WORKSPACE HAS MET THE DEV PROFILE'S OVERFLOW CHECKS and the first time the
//! wrap is the entire subject of the function rather than a consequence of it.
//! `cargo test` builds the dev profile, which takes `overflow-checks` from
//! `debug-assertions`, so `now_ms - deadline_ms` panics whenever `now` is
//! before the deadline. That is not an edge case here: it is every oracle case
//! where the deadline has not passed, which is three of the nine. The three
//! earlier instances were P17's elapsed subtraction and P20's rank product,
//! both real defects, and P04's advance, which was correct and intended and was
//! written `wrapping_sub` for exactly this reason.
//!
//! THE BACKOFF RETURNS `Option<u32>` WHERE THE C RETURNS A SENTINEL. The C's
//! `AT_BACKOFF_REFUSED` is `0xFFFFFFFFu`, which is a legal value of the return
//! type, so a caller that forgets to compare gets a delay of about forty-nine
//! days rather than an error. `None` cannot be used as a delay. The refusal is
//! the same refusal in both: a base of zero, a cap below the base, or a cap
//! above half the range, where the doubling would overflow and come back small.
//!
//! One difference is forced by the languages and is not a choice. The C takes
//! `const char *const *urcs` and reads `NULL` as "no table yet", which it says
//! is not an error. A slice cannot be null, so an **empty** slice says it here,
//! and the behaviour is the one the C gives for a null table and for a table of
//! no entries alike: every line that would have been unsolicited is
//! intermediate, which is what an engine with an empty table does.
#![cfg_attr(not(test), no_std)]

/// The three classes of step 5, plus the empty line, which is not a class.
///
/// A module pads its replies with blank lines, and a classifier that called one
/// an intermediate result would attach it to whatever command was in flight.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Class {
    FinalOk,
    FinalBad,
    Urc,
    Intermediate,
    Empty,
}

impl Class {
    /// The oracle's names, which are what the filter prints and the parity test
    /// compares.
    pub fn name(self) -> &'static str {
        match self {
            Class::FinalOk => "final_ok",
            Class::FinalBad => "final_bad",
            Class::Urc => "urc",
            Class::Intermediate => "intermediate",
            Class::Empty => "empty",
        }
    }
}

// The tables of chapter 11's step 5, visible here for the same reason the C
// keeps them visible: the module's own manual adds rows, and a table is where
// an addition goes rather than a chain of comparisons.
const FINAL_OK: &[&str] = &["OK", "CONNECT"];
const FINAL_BAD: &[&str] = &[
    "ERROR",
    "+CME ERROR:",
    "+CMS ERROR:",
    "NO CARRIER",
    "ABORTED",
];

fn match_any(line: &str, table: &[&str]) -> bool {
    table.iter().any(|prefix| line.starts_with(prefix))
}

/// Classify one line, without its line ending.
///
/// A PREFIX MATCH AND NOT AN EQUALITY TEST, which matters for exactly one row
/// in each table: `+CME ERROR:` is always followed by a number, and `CONNECT`
/// is followed by a baud rate on some modules and by nothing on others.
/// Matching the whole line would classify `+CME ERROR: 10` as intermediate and
/// attach it to the command as data. The oracle pins the known consequence
/// rather than hiding it: `OKAY` classifies as a success.
pub fn classify(line: &str, urcs: &[&str]) -> Class {
    if line.is_empty() {
        return Class::Empty;
    }
    if match_any(line, FINAL_OK) {
        return Class::FinalOk;
    }
    if match_any(line, FINAL_BAD) {
        return Class::FinalBad;
    }
    if match_any(line, urcs) {
        return Class::Urc;
    }
    Class::Intermediate
}

/// Has the deadline passed, with now and the deadline both free to have wrapped.
///
/// The contract is the C's and is the real one rather than the comfortable one:
/// this is correct as long as the two values are less than about twenty-four
/// days apart, which is half the wrap period. "Correct across the wrap" is only
/// true inside that bound.
pub fn due(now_ms: u32, deadline_ms: u32) -> bool {
    // Wrapping, so it is defined, and only the result is read as signed. The
    // subtraction on signed values would be undefined in C and a panic here,
    // which is the version that compiles, passes and then surprises somebody.
    (now_ms.wrapping_sub(deadline_ms) as i32) >= 0
}

/// The backoff of step 8: double, cap, then scatter.
///
/// THE RANDOM DRAW IS A PARAMETER AND THE CHAPTER'S SKETCH CALLS A FUNCTION.
/// The sketch reads `at_rand_small()` inside, which makes the result unprovable:
/// a test can only assert a band, never a value. Taking the draw as an argument
/// makes this pure and lets the oracle state exact values.
///
/// TWO THINGS THE SKETCH'S ARITHMETIC DOES THAT ARE WORTH KNOWING. The jitter
/// is applied after the cap, so **the answer can exceed `cap_ms`** by up to an
/// eighth of it: the cap bounds the doubling and not the result. And a cap above
/// half the range is refused, because the doubling would overflow and come back
/// as a plausible small number.
pub fn backoff_ms(attempt: u8, base_ms: u32, cap_ms: u32, rand_draw: u32) -> Option<u32> {
    if base_ms == 0 || cap_ms < base_ms {
        return None;
    }
    if cap_ms > u32::MAX / 2 {
        return None;
    }

    let mut d = base_ms;
    for _ in 0..attempt {
        if d >= cap_ms {
            break;
        }
        d <<= 1;
    }
    if d > cap_ms {
        d = cap_ms;
    }

    // The scatter, kept exactly as the chapter writes it: take an eighth off,
    // then add a draw from a quarter. The window is a quarter wide and sits an
    // eighth below, so the answer runs from seven eighths of the interval to
    // one and an eighth of it, which is why it can pass the cap.
    let span = if (d >> 2) != 0 { d >> 2 } else { 1 };
    Some(d - (d >> 3) + (rand_draw % span))
}

#[cfg(test)]
mod tests {
    use super::{backoff_ms, classify, due, Class};

    const URCS: &[&str] = &["+CEREG:", "+CSQ:", "+CMTI:", "+QIURC:"];

    #[test]
    fn a_prefix_match_accepts_a_rate_after_connect() {
        assert_eq!(classify("CONNECT 115200", URCS), Class::FinalOk);
        assert_eq!(classify("+CME ERROR: 10", URCS), Class::FinalBad);
    }

    #[test]
    fn the_known_consequence_of_a_prefix_match_is_pinned_and_not_hidden() {
        // OKAY is a success because the match is a prefix. NOT OK is not,
        // because the match is a prefix and not a search.
        assert_eq!(classify("OKAY", URCS), Class::FinalOk);
        assert_eq!(classify("NOT OK", URCS), Class::Intermediate);
    }

    #[test]
    fn an_empty_table_makes_every_unsolicited_line_intermediate() {
        assert_eq!(classify("+CEREG: 0,1", URCS), Class::Urc);
        assert_eq!(classify("+CEREG: 0,1", &[]), Class::Intermediate);
    }

    #[test]
    fn an_empty_line_is_not_a_class() {
        assert_eq!(classify("", URCS), Class::Empty);
    }

    #[test]
    fn the_deadline_is_exact_on_the_boundary_and_across_the_wrap() {
        assert!(due(1000, 1000));
        assert!(due(1000, 999));
        assert!(!due(1000, 1001));
        // Now has wrapped and the deadline has not, which is the case the
        // naive comparison gets wrong.
        assert!(due(5, 0xFFFF_FFFB));
        assert!(!due(0xFFFF_FFFB, 5));
    }

    #[test]
    fn the_naive_comparison_disagrees_exactly_where_the_wrap_is_crossed() {
        // Stated as arithmetic rather than as a belief: the two forms differ
        // on the wrapped pair above and agree on the unwrapped one.
        let now = 5u32;
        let deadline = 0xFFFF_FFFBu32;
        assert!(due(now, deadline));
        assert!(now < deadline, "the naive form reads this as not due");
        assert_eq!(due(2000, 1000), 2000 >= 1000);
    }

    #[test]
    fn the_jitter_is_applied_after_the_cap_so_the_answer_can_exceed_it() {
        // Ten doublings of 1000 are capped at 32000, an eighth off is 28000,
        // and the top of the window is 28000 + 7999.
        assert_eq!(backoff_ms(10, 1000, 32000, 0), Some(28000));
        assert_eq!(backoff_ms(10, 1000, 32000, 7999), Some(35999));
    }

    #[test]
    fn an_interval_too_small_to_take_an_eighth_from_falls_back_to_a_span_of_one() {
        assert_eq!(backoff_ms(0, 1, 1, 0), Some(1));
    }

    #[test]
    fn the_three_refusals_are_none_and_not_a_plausible_delay() {
        assert_eq!(backoff_ms(5, 0, 1000, 0), None);
        assert_eq!(backoff_ms(5, 2000, 1000, 0), None);
        assert_eq!(backoff_ms(1, 1000, 3_000_000_000, 0), None);
    }
}
