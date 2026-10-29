//! P07's filter: one request per line on stdin, one answer per line on stdout.
//!
//! Three verbs, one per function, each taking its counts before its data:
//!
//! ```text
//! C <fs_hz> <cycle_bit> <max_out> <n> <current digital>*n
//!     -> count=n uc=a,b,c
//! R <volts> <min_cycles> <count> <count charges>
//!     -> cycles=.. median=.. min=.. max=.. spread=.. energy=..
//! L <energy_uj> <cycles_per_day> <period_s> <sleep_ua> <sleep_v> <mah> <cell_v>
//!     -> cell_j=.. covered=.. uncovered=.. cycle_j=.. uncovered_j=.. days=..
//!        days_chapter=.. covers=..
//! ```
//!
//! EVERY FLOATING POINT VALUE CROSSES THIS PROTOCOL EXACTLY, which matters more
//! here than in any other filter in this volume: P07's parity test compares
//! `f64` with `==` and no tolerance, so a value that loses a bit on the wire
//! would fail a comparison the arithmetic passed. Values are parsed with
//! `str::parse::<f64>`, which is correctly rounded, and printed with `{:?}`,
//! which is the shortest form that reads back as the same bits. Python's own
//! `repr` and `float` are the same pair of guarantees on the other side.
//!
//! A request with anything left over is refused, the lesson P18's filter was
//! given on Monday 26 October 2026: ignoring a remainder lets a caller send one
//! thing and be answered about another.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p07_cyclecharge::{lifetime, per_cycle_charge, report, Battery, MAX_CYCLES};

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

fn joined(values: &[f64]) -> String {
    let mut out = String::new();
    for (i, v) in values.iter().enumerate() {
        if i > 0 {
            out.push(',');
        }
        out.push_str(&format!("{v:?}"));
    }
    out
}

fn do_charge(req: &mut Request) -> Option<String> {
    let fs_hz = req.f64()?;
    let cycle_bit = req.u8()?;
    let max_out = req.usize()?;
    let n = req.usize()?;
    let mut current = Vec::with_capacity(n);
    let mut digital = Vec::with_capacity(n);
    for _ in 0..n {
        current.push(req.f64()?);
        digital.push(req.u8()?);
    }
    // The C's max_out is the caller's array length, so the refusal for a trace
    // with more cycles than room is reachable through the protocol.
    let mut out = [0.0f64; MAX_CYCLES];
    if max_out > MAX_CYCLES {
        return None;
    }
    // Bound to a local first, because `Some(match f(..) {` with a call this
    // wide has two layouts rustfmt could defensibly choose and the author
    // cannot run it here. One name removes the question.
    let got = per_cycle_charge(&current, &digital, fs_hz, cycle_bit, &mut out[..max_out]);
    Some(match got {
        Ok(count) => format!("count={} uc={}", count, joined(&out[..count])),
        Err(e) => format!("REFUSED {}", e.name()),
    })
}

fn do_report(req: &mut Request) -> Option<String> {
    let volts = req.f64()?;
    let min_cycles = req.u32()?;
    let count = req.usize()?;
    let mut charges = Vec::with_capacity(count);
    for _ in 0..count {
        charges.push(req.f64()?);
    }
    Some(match report(&charges, volts, min_cycles) {
        Ok(r) => format!(
            "cycles={} median={:?} min={:?} max={:?} spread={:?} energy={:?}",
            r.cycles, r.median_uc, r.min_uc, r.max_uc, r.spread_pct, r.energy_median_uj
        ),
        Err(e) => format!("REFUSED {}", e.name()),
    })
}

fn do_lifetime(req: &mut Request) -> Option<String> {
    let b = Battery {
        energy_per_cycle_uj: req.f64()?,
        cycles_per_day: req.f64()?,
        cycle_period_s: req.f64()?,
        sleep_ua: req.f64()?,
        sleep_volts: req.f64()?,
        cell_mah: req.f64()?,
        cell_volts: req.f64()?,
    };
    Some(match lifetime(&b) {
        Ok(l) => format!(
            "cell_j={:?} covered={:?} uncovered={:?} cycle_j={:?} \
             uncovered_j={:?} days={:?} days_chapter={:?} covers={}",
            l.cell_j,
            l.covered_s,
            l.uncovered_s,
            l.cycle_j_per_day,
            l.uncovered_j_per_day,
            l.days,
            l.days_chapter,
            u8::from(l.covers_the_day)
        ),
        Err(e) => format!("REFUSED {}", e.name()),
    })
}

fn answer(verb: &str, req: &mut Request) -> Option<String> {
    match verb {
        "C" => do_charge(req),
        "R" => do_report(req),
        "L" => do_lifetime(req),
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
        if !matches!(verb, "C" | "R" | "L") {
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
