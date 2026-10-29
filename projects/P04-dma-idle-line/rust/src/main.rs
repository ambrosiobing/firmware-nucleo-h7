//! P04's filter: one request per line on stdin, one answer per line on stdout.
//!
//! One verb, because this project is one function:
//!
//! ```text
//! N <size> <old_pos> <pos>
//!     -> spans=n [off0=.. len0=.. [off1=.. len1=..]] peak=.. near_lap=..
//!        calls=.. deliveries=..
//! ```
//!
//! Each request runs on FRESH statistics, which is how the oracle states its
//! cases: one call, one answer, and the peak and the near-lap count therefore
//! describe that call alone rather than the run. The C's statistics accumulate
//! across calls and the parity test drives each case the same way, so the
//! protocol does not need a reset verb.
//!
//! A request with anything left over is refused, the lesson P18's filter was
//! given on Monday 26 October 2026: ignoring a remainder lets a caller send one
//! thing and be answered about another.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p04_rxspan::{next_spans, Delivery, Stats};

/// One request, already split into tokens, consumed left to right. A slice and
/// an index rather than a stored iterator, which is this volume's filter shape.
struct Request<'a> {
    tokens: &'a [&'a str],
    at: usize,
}

impl<'a> Request<'a> {
    fn new(tokens: &'a [&'a str]) -> Self {
        Request { tokens, at: 0 }
    }

    fn u16(&mut self) -> Option<u16> {
        let t = self.tokens.get(self.at)?;
        self.at += 1;
        t.parse::<u16>().ok()
    }

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

/// The spans flattened into the C's spelling, so one oracle field list serves
/// every language. The `Delivery` guarantees there is nothing to flatten that
/// is not there, which is the point of it; the wire format is where that ends.
fn spans_text(d: Delivery) -> String {
    let mut out = String::new();
    // flatten before enumerate, so the index counts the spans that are there
    // and the C's off0 and off1 line up. Also keeps clippy's manual_flatten
    // out of it, which a `for` over the array with an `if let` inside invites.
    for (i, s) in d.spans().iter().flatten().enumerate() {
        out.push_str(&format!(" off{i}={} len{i}={}", s.off, s.len));
    }
    out
}

fn do_next(req: &mut Request) -> Option<String> {
    let size = req.u16()?;
    let old_pos = req.u16()?;
    let pos = req.u16()?;
    let mut stats = Stats::default();
    Some(match next_spans(size, old_pos, pos, &mut stats) {
        Ok(d) => format!(
            "spans={}{} peak={} near_lap={} calls={} deliveries={}",
            d.count(),
            spans_text(d),
            stats.peak_advance,
            stats.near_lap,
            stats.calls,
            stats.deliveries
        ),
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
        if verb != "N" {
            let _ = writeln!(out, "BAD_VERB {verb}");
            return ExitCode::FAILURE;
        }
        let mut req = Request::new(rest);
        match do_next(&mut req) {
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
