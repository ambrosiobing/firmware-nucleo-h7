//! P18's filter: one request per line on stdin, one answer per line on stdout.
//!
//! It speaks the same protocol as `../../cpp/dspaccept_filter.cpp`, which is what
//! lets one parity test drive both with the same code. Six verbs, each taking
//! its counts before its data so the parser never guesses where a list ends:
//!
//! ```text
//! D <nstage> <m>          5*nstage coefficients, then m samples  (f64)
//! S <nstage> <m>          the same, in f32
//! P <nstage>              5*nstage coefficients: the smallest post shift
//! Q <nstage> <shift>      5*nstage coefficients: the Q15 conversion
//! R <nstage> <shift> <m>  5*nstage Q15 coefficients, then m Q15 samples
//! A <n>                   n got values, then n reference values
//! ```
//!
//! # On printing a double
//!
//! Values are printed with `{:?}`, which is the shortest form that round-trips
//! exactly. The C++ filter prints `%.17g`, which is a different spelling of the
//! same double: 0.1 is `0.1` here and `0.10000000000000001` there. The parity
//! test parses both, so the strings need not match and the values must, and that
//! is the right place for the difference to live. An infinity prints as `inf`
//! from both, which Python's `float` reads back.
//!
//! # On a request with anything left over
//!
//! It is refused, exactly as the C++ filter refuses it. Ignoring a remainder
//! would let a caller send one thing and be answered about another, and it
//! caught a real defect on Monday 26 October 2026: a test built a length
//! mismatch this protocol cannot express, and the spare value would have been
//! dropped in silence.
//!
//! The library this drives is `std`, because `sqrt` and `log10` are, and
//! `src/lib.rs` says why at the line where it matters.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p18_dspaccept::{
    accept_metrics, biquad_df1_q15, biquad_df2t_f32, biquad_df2t_f64, q15_coeffs, q15_post_shift,
    COEFF_PER_STAGE,
};

/// One request, already split into its tokens, consumed left to right.
///
/// A slice and an index rather than a stored iterator: the tokens borrow the
/// whole of stdin while the slice borrows a per-line vector, and two lifetimes
/// in a struct is a thing to get right rather than a thing to need.
struct Request<'a> {
    tokens: &'a [&'a str],
    at: usize,
}

impl<'a> Request<'a> {
    fn new(tokens: &'a [&'a str]) -> Self {
        Request { tokens, at: 0 }
    }

    fn token(&mut self) -> Option<&'a str> {
        let t = self.tokens.get(self.at)?;
        self.at += 1;
        Some(t)
    }

    fn count(&mut self) -> Option<usize> {
        self.token()?.parse::<usize>().ok()
    }

    fn integer(&mut self) -> Option<i32> {
        self.token()?.parse::<i32>().ok()
    }

    /// `str::parse` on an f64 accepts `inf` and `nan`, which matters here rather
    /// than being a detail: two of the oracle's cases carry a value that is not
    /// finite, and they are the two that exercise this project's own finite
    /// refusal. The C++ filter had to reach for `strtod` for the same reason,
    /// because the C++ stream extractor rejects both spellings.
    fn reals(&mut self, count: usize) -> Option<Vec<f64>> {
        let mut out = Vec::with_capacity(count);
        for _ in 0..count {
            out.push(self.token()?.parse::<f64>().ok()?);
        }
        Some(out)
    }

    fn q15s(&mut self, count: usize) -> Option<Vec<i16>> {
        let mut out = Vec::with_capacity(count);
        for _ in 0..count {
            out.push(self.token()?.parse::<i16>().ok()?);
        }
        Some(out)
    }

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

fn reals_line(values: &[f64]) -> String {
    let mut out = String::new();
    for (i, v) in values.iter().enumerate() {
        if i > 0 {
            out.push(' ');
        }
        out.push_str(&format!("{v:?}"));
    }
    out
}

fn ints_line(values: &[i16]) -> String {
    let mut out = String::new();
    for (i, v) in values.iter().enumerate() {
        if i > 0 {
            out.push(' ');
        }
        out.push_str(&v.to_string());
    }
    out
}

fn narrow(values: &[f64]) -> Vec<f32> {
    values.iter().map(|v| *v as f32).collect()
}

fn widen(values: &[f32]) -> Vec<f64> {
    values.iter().map(|v| f64::from(*v)).collect()
}

/// `None` means the request was malformed, which is a different thing from a
/// refusal: a refusal is an answer about a well formed request.
fn answer(verb: &str, req: &mut Request) -> Option<String> {
    match verb {
        "D" => {
            let nstage = req.count()?;
            let m = req.count()?;
            let coeffs = req.reals(nstage * COEFF_PER_STAGE)?;
            let data = req.reals(m)?;
            Some(match biquad_df2t_f64(&coeffs, nstage, &data) {
                Ok(out) => reals_line(&out),
                Err(e) => format!("REFUSED {}", e.name()),
            })
        }
        "S" => {
            let nstage = req.count()?;
            let m = req.count()?;
            let coeffs = narrow(&req.reals(nstage * COEFF_PER_STAGE)?);
            let data = narrow(&req.reals(m)?);
            let result = biquad_df2t_f32(&coeffs, nstage, &data);
            Some(match result {
                Ok(out) => reals_line(&widen(&out)),
                Err(e) => format!("REFUSED {}", e.name()),
            })
        }
        "P" => {
            let nstage = req.count()?;
            let coeffs = req.reals(nstage * COEFF_PER_STAGE)?;
            Some(match q15_post_shift(&coeffs, nstage) {
                Ok(shift) => format!("shift={shift}"),
                Err(e) => format!("REFUSED {}", e.name()),
            })
        }
        "Q" => {
            let nstage = req.count()?;
            let shift = req.integer()?;
            let coeffs = req.reals(nstage * COEFF_PER_STAGE)?;
            Some(match q15_coeffs(&coeffs, nstage, shift) {
                Ok(out) => ints_line(&out),
                Err(e) => format!("REFUSED {}", e.name()),
            })
        }
        "R" => {
            let nstage = req.count()?;
            let shift = req.integer()?;
            let m = req.count()?;
            let coeffs = req.q15s(nstage * COEFF_PER_STAGE)?;
            let data = req.q15s(m)?;
            Some(match biquad_df1_q15(&coeffs, nstage, shift, &data) {
                Ok((out, saturations)) => {
                    format!("sat={saturations} {}", ints_line(&out))
                }
                Err(e) => format!("REFUSED {}", e.name()),
            })
        }
        "A" => {
            let n = req.count()?;
            let got = req.reals(n)?;
            let reference = req.reals(n)?;
            Some(match accept_metrics(&got, &reference) {
                Ok(a) => format!(
                    "rel_rms={:?} snr_db={:?} exact={}",
                    a.rel_rms,
                    a.snr_db,
                    if a.exact { 1 } else { 0 }
                ),
                Err(e) => format!("REFUSED {}", e.name()),
            })
        }
        _ => None,
    }
}

fn main() -> ExitCode {
    let mut input = String::new();
    if io::stdin().read_to_string(&mut input).is_err() {
        println!("BAD_REQUEST");
        return ExitCode::FAILURE;
    }
    let stdout = io::stdout();
    let mut out = stdout.lock();

    for line in input.lines() {
        if line.trim().is_empty() {
            continue;
        }
        let tokens: Vec<&str> = line.split_whitespace().collect();
        let (first, rest) = tokens
            .split_first()
            .expect("a line that is not blank has a first token");
        let verb: &str = first;
        if !matches!(verb, "D" | "S" | "P" | "Q" | "R" | "A") {
            let _ = writeln!(out, "BAD_VERB {verb}");
            return ExitCode::FAILURE;
        }
        let mut req = Request::new(rest);
        match answer(verb, &mut req) {
            Some(text) => {
                if !req.exhausted() {
                    let _ = writeln!(out, "BAD_REQUEST trailing");
                    return ExitCode::FAILURE;
                }
                let _ = writeln!(out, "{text}");
            }
            None => {
                let _ = writeln!(out, "BAD_REQUEST");
                return ExitCode::FAILURE;
            }
        }
    }
    ExitCode::SUCCESS
}
