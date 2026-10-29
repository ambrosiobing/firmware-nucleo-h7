//! P10's filter: one request per line on stdin, one answer per line on stdout.
//!
//! One verb, because this project is one function:
//!
//! ```text
//! L <dt_s> <min_samples> <n> <current code>*n
//!     -> total=.. split=.. residual=.. runs=.. rejected=.. rejected_samples=..
//!        per0=.. per1=.. .. per7=..
//! ```
//!
//! The eight per-code charges are always printed, including the zeros, because
//! the chapter's codes 5 and 6 are unused and a trace that contains one is a
//! finding the report has to be able to show. Printing only the non-zero ones
//! would hide exactly the case the field exists for.
//!
//! Every floating point value crosses the protocol exactly: parsed with
//! `str::parse::<f64>`, which is correctly rounded, and printed with `{:?}`,
//! which is the shortest form that reads back as the same bits. The oracle
//! compares `total` and the per-code charges with no tolerance at all, so a
//! value losing one bit on the wire would fail a comparison the arithmetic
//! passed.
//!
//! A request with anything left over is refused, the lesson P18's filter was
//! given on Monday 26 October 2026.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p10_phaseledger::phase_ledger;

/// One request, already split into tokens, consumed left to right.
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

    fn f64(&mut self) -> Option<f64> {
        self.token()?.parse::<f64>().ok()
    }

    fn u32(&mut self) -> Option<u32> {
        self.token()?.parse::<u32>().ok()
    }

    fn usize(&mut self) -> Option<usize> {
        self.token()?.parse::<usize>().ok()
    }

    fn u8(&mut self) -> Option<u8> {
        self.token()?.parse::<u8>().ok()
    }

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

fn do_ledger(req: &mut Request) -> Option<String> {
    let dt_s = req.f64()?;
    let min_samples = req.u32()?;
    let n = req.usize()?;
    let mut current = Vec::with_capacity(n);
    let mut codes = Vec::with_capacity(n);
    for _ in 0..n {
        current.push(req.f64()?);
        codes.push(req.u8()?);
    }

    let got = phase_ledger(&current, &codes, dt_s, min_samples);
    Some(match got {
        Ok(l) => {
            let mut out = format!(
                "total={:?} split={:?} residual={:?} runs={} rejected={} \
                 rejected_samples={}",
                l.total, l.split, l.residual, l.runs, l.rejected, l.rejected_samples
            );
            // Enumerated over the array rather than indexed by a range, so
            // there is no question of needless_range_loop, which corrected two
            // of my loops earlier today.
            for (i, q) in l.per_code.iter().enumerate() {
                out.push_str(&format!(" per{i}={q:?}"));
            }
            out
        }
        Err(e) => format!("REFUSED {}", e.name()),
    })
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
        if verb != "L" {
            let _ = writeln!(out, "BAD_VERB {verb}");
            return ExitCode::FAILURE;
        }
        let mut req = Request::new(rest);
        match do_ledger(&mut req) {
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
