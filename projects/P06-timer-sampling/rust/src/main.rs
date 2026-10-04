//! P06's witness as a filter, speaking the one verb the C++ filter speaks.
//!
//! ```text
//! A <path> <fs> <nominal>   analyse that capture. Answers the fields, or
//!                           "REFUSED <status> edges=<n>".
//! ```
//!
//! The capture comes from a file and not from the line, which is the opposite of
//! every other filter here and deliberate twice over. A capture is thousands of
//! doubles, so inline it would be a line of tens of kilobytes, which is exactly
//! what broke the P08 filter. And a path is how the real witness receives a
//! capture: `rate.py` takes one on its command line.
//!
//! The file is one value per line, with blank lines and anything not starting
//! like a number skipped, which is the rule `rate.py`'s loader applies so a
//! header row costs nothing.
//!
//! Doubles are printed with seventeen significant digits, which round-trips a
//! double exactly. The parity test compares to a relative tolerance of 1e-12 and
//! not bit for bit, for the reason the library explains, but printing fewer
//! digits would have made the tolerance a property of the printing rather than
//! of the arithmetic.

use std::fs;
use std::io::{self, BufWriter, Read, Write};

use p06_rate::{analyse, Analysis, MAX_SAMPLES};

fn load(path: &str) -> Option<Vec<f64>> {
    let text = fs::read_to_string(path).ok()?;
    let mut out = Vec::new();
    for line in text.lines() {
        let at = line.trim_start();
        match at.chars().next() {
            Some(c) if c.is_ascii_digit() || c == '-' || c == '+' || c == '.' => {}
            _ => continue, // a header or a comment
        }
        // The first column, so a two-column capture reads the same way the
        // Python loader reads it.
        let field = at.split(',').next().unwrap_or(at).trim();
        if let Ok(v) = field.parse::<f64>() {
            out.push(v);
        }
        if out.len() >= MAX_SAMPLES {
            break;
        }
    }
    Some(out)
}

fn main() -> io::Result<()> {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input)?;

    let stdout = io::stdout();
    let mut out = BufWriter::new(stdout.lock());

    for line in input.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        if !line.starts_with('A') {
            writeln!(out, "BAD_VERB {}", &line[..1])?;
            continue;
        }

        let fields: Vec<&str> = line[1..].split_whitespace().collect();
        if fields.len() != 3 {
            writeln!(out, "BAD_REQUEST")?;
            continue;
        }
        let (path, fs_hz, nominal) = match (fields[1].parse::<f64>(), fields[2].parse::<f64>()) {
            (Ok(a), Ok(b)) => (fields[0], a, b),
            _ => {
                writeln!(out, "BAD_REQUEST")?;
                continue;
            }
        };

        let samples = match load(path) {
            Some(s) => s,
            None => {
                writeln!(out, "REFUSED no_such_capture")?;
                continue;
            }
        };

        let mut r = Analysis::default();
        if let Some(why) = analyse(&samples, fs_hz, nominal, &mut r) {
            writeln!(out, "REFUSED {} edges={}", why.name(), r.edges)?;
            continue;
        }

        writeln!(
            out,
            "edges={} duration_s={:.17e} resolution_s={:.17e} count_rate_hz={:.17e} \
             fit_period_s={:.17e} fit_rate_hz={:.17e} fit_rate_se_hz={:.17e} \
             interval_mean_s={:.17e} interval_sd_s={:.17e} interval_worst_s={:.17e} \
             interval_worst_dev_s={:.17e} missing_edges={} routes_agree={} \
             rate_within_tolerance={} jitter_within_limit={} \
             worst_interval_within_limit={} no_missing_edges={} pass={} \
             limited_by_instrument={}",
            r.edges,
            r.duration_s,
            r.resolution_s,
            r.count_rate_hz,
            r.fit_period_s,
            r.fit_rate_hz,
            r.fit_rate_se_hz,
            r.interval_mean_s,
            r.interval_sd_s,
            r.interval_worst_s,
            r.interval_worst_dev_s,
            r.missing_edges,
            r.routes_agree as u8,
            r.rate_within_tolerance as u8,
            r.jitter_within_limit as u8,
            r.worst_interval_within_limit as u8,
            r.no_missing_edges as u8,
            r.pass as u8,
            r.limited_by_instrument as u8
        )?;
    }
    out.flush()
}
