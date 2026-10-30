//! P19's filter: one request per line on stdin, one answer per line on stdout.
//!
//! Three verbs, one per function the oracle exercises:
//!
//! ```text
//! M <addr> <len> <line>   -> base=.. end=.. bytes=.. lines=.. head=0|1 tail=0|1
//! B <addr> <len> <line>   -> ok=1
//! R <base> <size>         -> field=N
//! ```
//!
//! and any of them can answer `REFUSED <name>` with one of the six names the C
//! uses.
//!
//! EVERY VALUE IS AN UNSIGNED 32 BIT INTEGER AND IS WRITTEN IN DECIMAL,
//! addresses included. Hexadecimal would read better to a person and would add
//! a second parser to every implementation for no gain, which is the decision
//! P01's filter already made for register words.
//!
//! THE LINE LENGTH TRAVELS IN EVERY RANGE REQUEST, because it is a parameter of
//! every function in all four implementations. The 32 bytes this part uses was
//! measured by P19's own placement run on the board on Saturday 3 October 2026,
//! and a figure measured on one part is not a constant of the arithmetic.
//!
//! `B` answers `ok=1` or a refusal and never `ok=0`: criterion 4 is a build
//! time assertion, so the only two outcomes are that it holds or that the
//! buffer is refused, and a filter that printed a flag would invite a caller to
//! read the flag and not the refusal.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p19_mpualign::{buffer_check, maint_range, region_encode};

/// One request, already split into tokens, consumed left to right.
struct Request<'a> {
    tokens: &'a [&'a str],
    at: usize,
}

impl<'a> Request<'a> {
    fn new(tokens: &'a [&'a str]) -> Self {
        Request { tokens, at: 0 }
    }

    fn u32(&mut self) -> Option<u32> {
        let t = self.tokens.get(self.at)?;
        self.at += 1;
        t.parse::<u32>().ok()
    }

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

fn do_maint(req: &mut Request) -> Option<String> {
    let addr = req.u32()?;
    let len = req.u32()?;
    let line = req.u32()?;
    Some(match maint_range(addr, len, line) {
        Ok(r) => format!(
            "base={} end={} bytes={} lines={} head={} tail={}",
            r.base,
            r.end,
            r.bytes,
            r.lines,
            u8::from(r.head_shared),
            u8::from(r.tail_shared)
        ),
        Err(e) => format!("REFUSED {}", e.name()),
    })
}

fn do_buffer(req: &mut Request) -> Option<String> {
    let addr = req.u32()?;
    let len = req.u32()?;
    let line = req.u32()?;
    Some(match buffer_check(addr, len, line) {
        Ok(()) => "ok=1".to_string(),
        Err(e) => format!("REFUSED {}", e.name()),
    })
}

fn do_region(req: &mut Request) -> Option<String> {
    let base = req.u32()?;
    let size = req.u32()?;
    Some(match region_encode(base, size) {
        Ok(field) => format!("field={field}"),
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
        let mut req = Request::new(rest);
        let answer = match verb {
            "M" => do_maint(&mut req),
            "B" => do_buffer(&mut req),
            "R" => do_region(&mut req),
            _ => {
                let _ = writeln!(out, "BAD_VERB {verb}");
                return ExitCode::FAILURE;
            }
        };
        match answer {
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
