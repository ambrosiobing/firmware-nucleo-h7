//! P17's filter: one request per line on stdin, one answer per line on stdout.
//!
//! Four verbs, each taking its counts before its data so the parser never
//! guesses where a list ends:
//!
//! ```text
//! E <start> <end> <width> <down>   elapsed ticks, 0 up and 1 down
//! W <hz> <width>                   the wrap period in nanoseconds
//! S <raw> <overhead>               the interval less the instrument's cost
//! R <n> <n samples>                the first run and the median of the rest
//! ```
//!
//! A request with anything left over is refused, the lesson P18's filter was
//! given on Monday 26 October 2026: ignoring a remainder lets a caller send one
//! thing and be answered about another, and it caught a real defect there.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p17_tbmath::{elapsed, report, subtract_overhead, wrap_ns, Direction};

/// One request, already split into tokens, consumed left to right. A slice and
/// an index rather than a stored iterator, which is P18's filter's shape and
/// avoids two lifetimes in one struct.
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

    fn u32(&mut self) -> Option<u32> {
        self.token()?.parse::<u32>().ok()
    }

    fn usize(&mut self) -> Option<usize> {
        self.token()?.parse::<usize>().ok()
    }

    fn f64(&mut self) -> Option<f64> {
        self.token()?.parse::<f64>().ok()
    }

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

fn answer(verb: &str, req: &mut Request) -> Option<String> {
    match verb {
        "E" => {
            let start = req.u32()?;
            let end = req.u32()?;
            let width = req.u32()?;
            let down = req.u32()?;
            let dir = match down {
                0 => Direction::Up,
                1 => Direction::Down,
                // The C refuses a direction that is neither, and the parity
                // test drives that case, so it is an answer and not a
                // malformed request.
                _ => return Some("REFUSED direction".to_string()),
            };
            Some(match elapsed(start, end, width, dir) {
                Ok(ticks) => format!("ticks={ticks}"),
                Err(e) => format!("REFUSED {}", e.name()),
            })
        }
        "W" => {
            let hz = req.f64()?;
            let width = req.u32()?;
            Some(match wrap_ns(hz, width) {
                Ok(ns) => format!("ns={ns}"),
                Err(e) => format!("REFUSED {}", e.name()),
            })
        }
        "S" => {
            let raw = req.u32()?;
            let overhead = req.u32()?;
            Some(match subtract_overhead(raw, overhead) {
                Ok(ticks) => format!("ticks={ticks}"),
                Err(e) => format!("REFUSED {}", e.name()),
            })
        }
        "R" => {
            let n = req.usize()?;
            let mut samples = Vec::with_capacity(n);
            for _ in 0..n {
                samples.push(req.u32()?);
            }
            Some(match report(&samples) {
                Ok(r) => format!(
                    "first={} median={} smallest={} largest={} count={}",
                    r.first, r.median, r.smallest, r.largest, r.count
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
        if !matches!(verb, "E" | "W" | "S" | "R") {
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
