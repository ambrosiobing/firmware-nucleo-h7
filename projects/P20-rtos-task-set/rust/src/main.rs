//! P20's filter: one request per line on stdin, one answer per line on stdout.
//!
//! Four verbs, each taking its counts before its data so the parser never
//! guesses where a list ends:
//!
//! ```text
//! R <cycles_per_us> <npairs> <cycles repeat>*npairs
//!     record a run, then report and judge it
//! B <over> <max_us> <count> <npairs> <bin count>*npairs
//!     a histogram built field by field, then reported and judged
//! D <count_before> <cycles> <cycles_per_us>
//!     one sample onto a counter preset to count_before
//! S <budget> <free>
//!     one task's stack from the kernel's smallest free figure
//! ```
//!
//! Samples arrive run-length encoded, as a cycle figure and a repeat, because
//! the chapter's run is a million releases and the oracle stores a recipe
//! rather than a list.
//!
//! `R` and `B` answer with the report and the verdict on one line, judged
//! against table 20.4, which the library carries as constants. `D` answers `ok`
//! or a refusal, and `S` answers the three stack figures.
//!
//! A request with anything left over is refused, the lesson P18's filter was
//! given on Monday 26 October 2026: ignoring a remainder lets a caller send one
//! thing and be answered about another.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p20_latstat::{stack, verdict, Budget, Hist, Report, Verdicts, BINS};

/// One request, already split into tokens, consumed left to right. A slice and
/// an index rather than a stored iterator, which is P17's and P18's filter's
/// shape and avoids two lifetimes in one struct.
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

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

fn judged(r: &Report, v: &Verdicts) -> String {
    format!(
        "median={} median_resolved={} p999={} p999_resolved={} max={} \
         over={} count={} vmedian={} vp999={} vmax={}",
        r.median_us,
        u8::from(r.median_resolved),
        r.p999_us,
        u8::from(r.p999_resolved),
        r.max_us,
        r.over,
        r.count,
        v.median.code(),
        v.p999.code(),
        v.max.code()
    )
}

fn answer_for(h: &Hist) -> String {
    match h.report() {
        Ok(r) => {
            let v = verdict(&r, &Budget::default());
            judged(&r, &v)
        }
        Err(e) => format!("REFUSED {}", e.name()),
    }
}

fn do_run(req: &mut Request) -> Option<String> {
    let cycles_per_us = req.u32()?;
    let pairs = req.usize()?;
    let mut h = Hist::new();
    for _ in 0..pairs {
        let cycles = req.u32()?;
        let repeat = req.u32()?;
        for _ in 0..repeat {
            if let Err(e) = h.record(cycles, cycles_per_us) {
                // A refusal while recording is an answer about a well formed
                // request, so it is reported rather than treated as malformed.
                return Some(format!("REFUSED {}", e.name()));
            }
        }
    }
    Some(answer_for(&h))
}

fn do_built(req: &mut Request) -> Option<String> {
    let over = req.u32()?;
    let max_us = req.u32()?;
    let count = req.u32()?;
    let pairs = req.usize()?;
    let mut h = Hist::new();
    h.over = over;
    h.max_us = max_us;
    h.count = count;
    for _ in 0..pairs {
        let bin = req.usize()?;
        let n = req.u32()?;
        if bin >= BINS {
            return None;
        }
        h.bins[bin] = n;
    }
    Some(answer_for(&h))
}

fn do_one_sample(req: &mut Request) -> Option<String> {
    let count_before = req.u32()?;
    let cycles = req.u32()?;
    let cycles_per_us = req.u32()?;
    let mut h = Hist::new();
    h.count = count_before;
    Some(match h.record(cycles, cycles_per_us) {
        Ok(()) => "ok".to_string(),
        Err(e) => format!("REFUSED {}", e.name()),
    })
}

fn do_stack(req: &mut Request) -> Option<String> {
    let budget = req.u32()?;
    let free = req.u32()?;
    Some(match stack(budget, free) {
        Ok(s) => format!(
            "used={} permil={} within={}",
            s.used,
            s.permil,
            u8::from(s.within)
        ),
        Err(e) => format!("REFUSED {}", e.name()),
    })
}

fn answer(verb: &str, req: &mut Request) -> Option<String> {
    match verb {
        "R" => do_run(req),
        "B" => do_built(req),
        "D" => do_one_sample(req),
        "S" => do_stack(req),
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
        if !matches!(verb, "R" | "B" | "D" | "S") {
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
