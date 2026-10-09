//! P06's rate witness in Rust: two independent routes that must agree.
//!
//! The same arithmetic, in the same order, as `../c/rate.c` and
//! `python/firmkit/rate.py`. The order is not a matter of style: a sum
//! accumulated differently gives a different double, and the parity test
//! compares these numbers.
//!
//! **The half of P06 that can be proven.** The three acquisition back ends
//! refuse at run time rather than guessing a converter, timer or transfer
//! engine setting RM0455 governs. The witness needs none of that, and it is the
//! piece that decides whether a clean square wave at a plausible wrong rate is
//! reported as a pass, which is this project's whole subject.
//!
//! # This is the one crate in the workspace that is not `no_std`
//!
//! And the reason is worth more than the exception. `f64::sqrt`, `f64::abs` and
//! `f64::is_nan` are in `std`, not in `core`: Rust's core library has no
//! floating-point maths at all, because those functions live in the platform's
//! libm and `core` does not assume a platform. A `no_std` version of this file
//! would need the `libm` crate as a dependency.
//!
//! So the choice was between a dependency added to preserve a pattern, and an
//! honest exception. The witness runs on a host: it reads a recording the MCC
//! 118 or the PPK2 produced and decides whether a rate claim stands. It has no
//! reason to run on the board. The other five crates are `no_std` because they
//! are code the board will link; this one is `std` because it is not.
//!
//! The C is different and that difference is itself the finding. `rate.c`
//! compiles unchanged for the target, because C's `<math.h>` is part of the
//! freestanding-adjacent world the toolchain ships with and newlib provides
//! `sqrt`. The same program is portable to the target in C and is not in Rust
//! without a crate. That is a real cost of the language on this part, in the one
//! direction people do not usually expect, and `cargo build --target
//! thumbv7em-none-eabihf` therefore omits this package by name in `code.yml`.
//!
//! # On floating point
//!
//! The four implementations are compared to a relative tolerance of 1e-12 and
//! not bit for bit. Double arithmetic is deterministic, but the contraction of a
//! multiply and an add into one fused instruction is not the same across
//! toolchains: gcc may contract where rustc does not, and the Cortex-M7's
//! floating point unit has a fused multiply-add where a host may not. The C and
//! C++ are compiled with `-ffp-contract=off` to remove the question where it can
//! be removed, and the tolerance covers what remains. The pass criteria are
//! 0.1 percent, so 1e-12 is nine orders of magnitude tighter than anything the
//! measurement claims.

#![forbid(unsafe_code)]

/// The criteria from `docs/measurement.md`, written before any hardware was
/// connected. Changing a number here is a change to the claim, so it is a
/// change to that document and to the other three implementations.
pub const NOMINAL_HZ_DEFAULT: f64 = 1000.0;
pub const RATE_TOLERANCE_FRACTION: f64 = 0.001;
pub const JITTER_SD_MAX_S: f64 = 10e-6;
pub const WORST_INTERVAL_DEV_MAX_S: f64 = 50e-6;
pub const MAX_MISSING_EDGES: usize = 0;

pub const MAX_SAMPLES: usize = 65536;
pub const MAX_EDGES: usize = 4096;

/// Why an analysis refused.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Refused {
    /// Fewer than three edges; there is nothing to measure.
    TooFewEdges,
    /// A flat recording has no edges to find.
    NoSwing,
    /// More edges than `MAX_EDGES`.
    TooManyEdges,
    /// A sample rate of zero or less.
    Args,
}

impl Refused {
    /// The name the other three implementations print.
    pub fn name(self) -> &'static str {
        match self {
            Refused::TooFewEdges => "too_few_edges",
            Refused::NoSwing => "no_swing",
            Refused::TooManyEdges => "too_many_edges",
            Refused::Args => "args",
        }
    }
}

/// Everything `docs/measurement.md` asks for, and the verdict on each.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Analysis {
    pub sample_rate_hz: f64,
    pub duration_s: f64,
    pub edges: usize,
    pub nominal_hz: f64,
    pub resolution_s: f64,

    /// Route one: edges minus one over the span.
    pub count_rate_hz: f64,
    /// Route two: the straight-line gradient.
    pub fit_period_s: f64,
    pub fit_rate_hz: f64,
    pub fit_rate_se_hz: f64,

    pub interval_mean_s: f64,
    pub interval_sd_s: f64,
    pub interval_worst_s: f64,
    pub interval_worst_dev_s: f64,
    pub missing_edges: usize,

    pub routes_agree: bool,
    pub rate_within_tolerance: bool,
    pub jitter_within_limit: bool,
    pub worst_interval_within_limit: bool,
    pub no_missing_edges: bool,
    pub pass: bool,
    /// The measured spread is below half the witness sample period, so it is
    /// limited by the instrument and not by the board. A claim can never be
    /// finer than the instrument, and saying so in the result beats leaving a
    /// reader to work it out.
    pub limited_by_instrument: bool,
}

impl Default for Analysis {
    fn default() -> Self {
        Self {
            sample_rate_hz: 0.0,
            duration_s: 0.0,
            edges: 0,
            nominal_hz: 0.0,
            resolution_s: 0.0,
            count_rate_hz: 0.0,
            fit_period_s: 0.0,
            fit_rate_hz: 0.0,
            fit_rate_se_hz: 0.0,
            interval_mean_s: 0.0,
            interval_sd_s: 0.0,
            interval_worst_s: 0.0,
            interval_worst_dev_s: 0.0,
            missing_edges: 0,
            routes_agree: false,
            rate_within_tolerance: false,
            jitter_within_limit: false,
            worst_interval_within_limit: false,
            no_missing_edges: false,
            pass: false,
            limited_by_instrument: false,
        }
    }
}

/// Edge times in seconds, rising and falling alike, by linear interpolation
/// across the midpoint of a Schmitt-style pair of thresholds taken from the
/// observed swing rather than assumed to be rail voltages, so a recording
/// through an attenuator or with an offset still works. The marker toggles once
/// per sample, so every crossing in either direction is a sampling instant.
/// Interpolation is what gets an edge time below one sample period, which
/// criterion 2 depends on.
pub fn marker_edges(samples: &[f64], fs: f64) -> Result<Vec<f64>, Refused> {
    if fs <= 0.0 {
        return Err(Refused::Args);
    }
    // Sixteen samples is the Python's own floor: fewer is a fragment, not a
    // recording of a square wave.
    if samples.len() < 16 {
        return Ok(Vec::new());
    }

    let mut lo_obs = samples[0];
    let mut hi_obs = samples[0];
    for &s in &samples[1..] {
        if s < lo_obs {
            lo_obs = s;
        }
        if s > hi_obs {
            hi_obs = s;
        }
    }
    let swing = hi_obs - lo_obs;
    if swing <= 0.0 {
        return Err(Refused::NoSwing);
    }

    let low = lo_obs + 0.3 * swing;
    let high = lo_obs + 0.7 * swing;
    let mid = 0.5 * (low + high);

    // Both polarities, with the Schmitt pair as hysteresis: a rise counts only
    // from an established low, a fall only from an established high. The
    // starting level is the first sample against the midpoint and nothing is
    // counted until the first crossing. Rising edges alone until Friday
    // 9 October 2026; see marker.c for why that matched a pulse the witness saw
    // only in fragments.
    let mut edges: Vec<f64> = Vec::new();
    let mut is_high = samples[0] >= mid;

    for i in 1..samples.len() {
        let prev = samples[i - 1];
        let cur = samples[i];
        let rise = !is_high && cur >= high;
        let fall = is_high && cur < low;
        if !rise && !fall {
            continue;
        }
        let frac = if cur != prev {
            ((mid - prev) / (cur - prev)).clamp(0.0, 1.0)
        } else {
            0.0
        };
        if edges.len() >= MAX_EDGES {
            return Err(Refused::TooManyEdges);
        }
        edges.push(((i - 1) as f64 + frac) / fs);
        is_high = rise;
    }
    Ok(edges)
}

/// Least squares gradient, intercept and the standard error of the gradient,
/// written out so the witness needs nothing beyond the standard library.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Fit {
    pub slope: f64,
    pub intercept: f64,
    pub slope_se: f64,
}

pub fn fit_line(x: &[f64], y: &[f64]) -> Option<Fit> {
    let count = x.len();
    if count < 3 || y.len() != count {
        return None;
    }
    let n = count as f64;

    // Summed in the same order as the Python and the C, separately for x and y,
    // because a single fused loop would accumulate in a different order and
    // give a different double.
    let mut sx = 0.0;
    for &v in x {
        sx += v;
    }
    let mut sy = 0.0;
    for &v in y {
        sy += v;
    }
    let mx = sx / n;
    let my = sy / n;

    let mut sxx = 0.0;
    for &v in x {
        let d = v - mx;
        sxx += d * d;
    }
    if sxx == 0.0 {
        return None;
    }

    let mut sxy = 0.0;
    for i in 0..count {
        sxy += (x[i] - mx) * (y[i] - my);
    }
    let slope = sxy / sxx;
    let intercept = my - slope * mx;

    let mut ss = 0.0;
    for i in 0..count {
        let r = y[i] - (slope * x[i] + intercept);
        ss += r * r;
    }
    let s2 = ss / (n - 2.0);

    Some(Fit {
        slope,
        intercept,
        slope_se: (s2 / sxx).sqrt(),
    })
}

/// The whole analysis. On a refusal the `Analysis` handed back through `out`
/// still carries what was known before it, so a caller can print the edge
/// count.
pub fn analyse(samples: &[f64], fs: f64, nominal_hz: f64, out: &mut Analysis) -> Option<Refused> {
    *out = Analysis {
        sample_rate_hz: fs,
        duration_s: if fs > 0.0 {
            samples.len() as f64 / fs
        } else {
            0.0
        },
        nominal_hz,
        resolution_s: if fs > 0.0 { 1.0 / fs } else { f64::NAN },
        ..Default::default()
    };

    let edges = match marker_edges(samples, fs) {
        Ok(e) => e,
        Err(why) => return Some(why),
    };
    out.edges = edges.len();
    if edges.len() < 3 {
        out.pass = false;
        return Some(Refused::TooFewEdges);
    }
    let n = edges.len();

    // Route one: the count.
    let span = edges[n - 1] - edges[0];
    out.count_rate_hz = if span > 0.0 {
        (n - 1) as f64 / span
    } else {
        f64::NAN
    };

    // Route two: the fit.
    let index: Vec<f64> = (0..n).map(|i| i as f64).collect();
    let fit = fit_line(&index, &edges);
    let period = fit.map_or(f64::NAN, |f| f.slope);
    let period_se = fit.map_or(f64::NAN, |f| f.slope_se);
    out.fit_period_s = period;
    out.fit_rate_hz = if period != 0.0 {
        1.0 / period
    } else {
        f64::NAN
    };
    // dRate = dPeriod / period squared.
    out.fit_rate_se_hz = if period != 0.0 {
        period_se / (period * period)
    } else {
        f64::NAN
    };

    let ivs = n - 1;
    let mut sum_iv = 0.0;
    for i in 0..ivs {
        sum_iv += edges[i + 1] - edges[i];
    }
    let mean_iv = sum_iv / ivs as f64;
    out.interval_mean_s = mean_iv;

    let var = if ivs > 1 {
        let mut acc = 0.0;
        for i in 0..ivs {
            let d = (edges[i + 1] - edges[i]) - mean_iv;
            acc += d * d;
        }
        acc / (ivs - 1) as f64
    } else {
        0.0
    };
    out.interval_sd_s = var.sqrt();

    let nominal_iv = 1.0 / nominal_hz;
    let mut worst = edges[1] - edges[0];
    let mut worst_dev = (worst - nominal_iv).abs();
    for i in 1..ivs {
        let v = edges[i + 1] - edges[i];
        let dev = (v - nominal_iv).abs();
        // Strictly greater, so the first of two equally bad intervals wins,
        // which is what Python's max does.
        if dev > worst_dev {
            worst = v;
            worst_dev = dev;
        }
    }
    out.interval_worst_s = worst;
    out.interval_worst_dev_s = worst_dev;

    // A dropped edge shows as an interval near an integer multiple of nominal,
    // a doubled edge as one near a half. Both are missing edges.
    //
    // `round_ties_even` and not `round`: Rust's `f64::round` rounds half away
    // from zero, while Python's `round` and C's `nearbyint` round half to even.
    // That function was stabilised in Rust 1.77, which is why this crate's
    // rust-version is 1.77 where the other four say 1.75: the alternative was
    // to hand-roll the tie rule, and a hand-rolled rounding rule in a file
    // whose whole job is agreeing with three other implementations is the wrong
    // place to save a version.
    // The three would disagree only when the ratio is exactly a half integer,
    // which a real recording never produces and a synthetic one easily could,
    // and a parity test comparing synthetic captures is exactly where that
    // would surface.
    let mut missing = 0usize;
    for i in 0..ivs {
        let v = edges[i + 1] - edges[i];
        let k = v / nominal_iv;
        let nearest = k.round_ties_even();
        if nearest >= 2.0 && (k - nearest).abs() < 0.25 {
            missing += nearest as usize - 1;
        } else if nearest == 0.0 || (k < 0.75 && k > 0.0) {
            missing += 1;
        }
    }
    out.missing_edges = missing;

    // The two routes must agree, or the analysis is wrong and not the firmware.
    out.routes_agree = !out.fit_rate_hz.is_nan()
        && (out.count_rate_hz - out.fit_rate_hz).abs() < 0.01 * nominal_hz;

    out.rate_within_tolerance =
        (out.fit_rate_hz - nominal_hz).abs() <= RATE_TOLERANCE_FRACTION * nominal_hz;
    out.jitter_within_limit = out.interval_sd_s <= JITTER_SD_MAX_S;
    out.worst_interval_within_limit = out.interval_worst_dev_s <= WORST_INTERVAL_DEV_MAX_S;
    // Criterion 4 is a budget: at most MAX_MISSING_EDGES missing edges.
    // Clippy is right that with the budget at zero and missing_edges a usize,
    // this comparison can only ever be equality, and it suggests == instead.
    // That suggestion is refused on purpose, and the reason is worth more than
    // the lint: == MAX_MISSING_EDGES inverts the criterion the moment the
    // budget stops being zero, because a budget of one would then report a
    // capture with no missing edges at all as a failure. The comparison that
    // survives a change to the constant is the one all four implementations
    // write, so the lint is silenced by name at this one statement rather than
    // the criterion being rewritten to suit it. The C, C++ and Python read the
    // same and have no equivalent lint to answer.
    #[allow(clippy::absurd_extreme_comparisons)]
    {
        out.no_missing_edges = out.missing_edges <= MAX_MISSING_EDGES;
    }

    out.pass = out.rate_within_tolerance
        && out.jitter_within_limit
        && out.worst_interval_within_limit
        && out.no_missing_edges
        && out.routes_agree;

    out.limited_by_instrument = out.interval_sd_s < out.resolution_s / 2.0;
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A waveform carrying `hz` marker EDGES per second, sampled at `fs`, for
    /// `seconds`. The marker toggles once per sample, so a clean kilohertz of
    /// sampling instants is a 500 Hz square wave: `hz` edges, rising and falling
    /// alike, in each second. Built at `hz / 2` for that reason, the same
    /// convention the Python tests use since Friday 9 October 2026; before then
    /// this was a square wave at `hz` and the analyser counted rising edges only,
    /// and the two changed together.
    fn square(hz: f64, fs: f64, seconds: f64) -> Vec<f64> {
        let n = (fs * seconds) as usize;
        (0..n)
            .map(|i| {
                let t = i as f64 / fs;
                let phase = (t * hz * 0.5).fract();
                if phase < 0.5 {
                    0.0
                } else {
                    3.3
                }
            })
            .collect()
    }

    #[test]
    fn a_clean_kilohertz_square_wave_passes_every_criterion() {
        let samples = square(1000.0, 100_000.0, 0.5);
        let mut r = Analysis::default();
        assert_eq!(analyse(&samples, 100_000.0, 1000.0, &mut r), None);
        assert!(r.edges > 400, "only {} edges in half a second", r.edges);
        assert!(r.rate_within_tolerance, "fit rate {}", r.fit_rate_hz);
        assert!(
            r.routes_agree,
            "count {} fit {}",
            r.count_rate_hz, r.fit_rate_hz
        );
        assert_eq!(r.missing_edges, 0);
        assert!(r.pass);
    }

    /// The case the whole project exists for: a clean wave at the wrong rate.
    /// It must fail the rate criterion while passing the jitter one, because a
    /// witness that reported this as a pass would be worthless.
    #[test]
    fn a_clean_square_wave_at_the_wrong_rate_fails_on_the_rate_and_not_on_jitter() {
        let samples = square(1100.0, 100_000.0, 0.5);
        let mut r = Analysis::default();
        assert_eq!(analyse(&samples, 100_000.0, 1000.0, &mut r), None);
        assert!(!r.rate_within_tolerance, "1100 Hz was called 1000 Hz");
        assert!(r.routes_agree, "both routes should agree on the wrong rate");
        assert!(!r.pass);
        assert!(
            (r.fit_rate_hz - 1100.0).abs() < 1.0,
            "the fit found {} and not 1100",
            r.fit_rate_hz
        );
    }

    #[test]
    fn a_flat_recording_is_refused_rather_than_reported_as_perfect() {
        let samples = vec![1.65; 1000];
        let mut r = Analysis::default();
        assert_eq!(
            analyse(&samples, 100_000.0, 1000.0, &mut r),
            Some(Refused::NoSwing)
        );
        assert!(!r.pass);
    }

    #[test]
    fn a_fragment_is_refused_for_having_too_few_edges() {
        let samples = square(1000.0, 100_000.0, 0.0015);
        let mut r = Analysis::default();
        assert_eq!(
            analyse(&samples, 100_000.0, 1000.0, &mut r),
            Some(Refused::TooFewEdges)
        );
        assert!(!r.pass);
    }

    #[test]
    fn the_two_routes_are_computed_differently_and_must_still_agree() {
        // The count is edges minus one over the span; the fit is a gradient.
        // On a clean wave they agree to well inside the one percent the check
        // allows, and that margin is worth asserting rather than assuming.
        let samples = square(1000.0, 100_000.0, 0.25);
        let mut r = Analysis::default();
        assert_eq!(analyse(&samples, 100_000.0, 1000.0, &mut r), None);
        let gap = (r.count_rate_hz - r.fit_rate_hz).abs();
        assert!(gap < 0.001, "the two routes differ by {} Hz", gap);
    }

    #[test]
    fn the_fit_of_a_straight_line_has_no_residual() {
        let x: Vec<f64> = (0..50).map(|i| i as f64).collect();
        let y: Vec<f64> = x.iter().map(|v| 3.0 * v + 7.0).collect();
        let f = fit_line(&x, &y).expect("fits");
        assert!((f.slope - 3.0).abs() < 1e-12, "slope {}", f.slope);
        assert!(
            (f.intercept - 7.0).abs() < 1e-12,
            "intercept {}",
            f.intercept
        );
        assert!(f.slope_se < 1e-12, "a perfect fit has no standard error");
    }

    #[test]
    fn fewer_than_three_points_cannot_be_fitted() {
        assert_eq!(fit_line(&[0.0, 1.0], &[0.0, 1.0]), None);
    }
}
