//! P11's filter: one request per line on stdin, one answer per line on stdout.
//!
//! Three verbs, one per piece of chapter 11 that needs no module:
//!
//! ```text
//! C <n_urcs> <urc hex>*n <line length> <line hex if the length is not zero>
//!     -> class=final_ok | final_bad | urc | intermediate | empty
//! D <now_ms> <deadline_ms>            -> due=1 | due=0
//! B <attempt> <base_ms> <cap_ms> <rand>   -> delay=NNN | REFUSED backoff
//! ```
//!
//! THE LINES ARE HEX AND NOT TEXT, AND THAT IS NOT DECORATION. Nine of the
//! oracle's seventeen classify cases contain a space, four of them contain a
//! comma or a double quote, `+CMTI: "SM",3` among them, and one is **empty**,
//! which is a class of its own here. **Ten of the seventeen therefore cannot
//! cross a whitespace-separated protocol at all**, and only seven can. A
//! filter that cannot carry a case cannot test it, which this volume
//! has already paid for once: an oracle case the C's signature could not
//! express cost two red commits, and the answer then was to change the
//! signature rather than exempt the case. So every line and every unsolicited
//! prefix crosses as hex, the way P05's filter carries a frame.
//!
//! THE LENGTH IS SENT AS WELL AS THE HEX, for the empty line. A whitespace
//! split cannot produce an empty token, so the length decides whether a hex
//! token follows at all, and when one does its decoded length must equal the
//! stated one. That makes a truncated request a refusal rather than a shorter
//! line quietly classified.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p11_atparse::{backoff_ms, classify, due};

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

    fn u32(&mut self) -> Option<u32> {
        self.token()?.parse::<u32>().ok()
    }

    fn u8(&mut self) -> Option<u8> {
        self.token()?.parse::<u8>().ok()
    }

    fn usize(&mut self) -> Option<usize> {
        self.token()?.parse::<usize>().ok()
    }

    /// A length and then, only if it is not zero, the hex of that many bytes.
    fn hex_string(&mut self) -> Option<String> {
        let want = self.usize()?;
        if want == 0 {
            return Some(String::new());
        }
        let text = self.token()?;
        if text.len() != want * 2 {
            return None;
        }
        let mut bytes = Vec::with_capacity(want);
        for i in 0..want {
            // get and not an index, so a pair that is not a character
            // boundary answers None rather than panicking.
            let pair = text.get(i * 2..i * 2 + 2)?;
            bytes.push(u8::from_str_radix(pair, 16).ok()?);
        }
        String::from_utf8(bytes).ok()
    }

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

fn do_classify(req: &mut Request) -> Option<String> {
    let count = req.usize()?;
    let mut urcs = Vec::with_capacity(count);
    for _ in 0..count {
        urcs.push(req.hex_string()?);
    }
    let line = req.hex_string()?;
    let table: Vec<&str> = urcs.iter().map(|s| s.as_str()).collect();
    Some(format!("class={}", classify(&line, &table).name()))
}

fn do_due(req: &mut Request) -> Option<String> {
    let now = req.u32()?;
    let deadline = req.u32()?;
    Some(format!("due={}", u8::from(due(now, deadline))))
}

fn do_backoff(req: &mut Request) -> Option<String> {
    let attempt = req.u8()?;
    let base = req.u32()?;
    let cap = req.u32()?;
    let draw = req.u32()?;
    // The refusal is a word and not 0xFFFFFFFF. The C's sentinel is a legal
    // delay, so the protocol would carry the refusal as a plausible number and
    // a test comparing numbers could not tell the two apart.
    Some(match backoff_ms(attempt, base, cap, draw) {
        Some(ms) => format!("delay={ms}"),
        None => "REFUSED backoff".to_string(),
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
            "C" => do_classify(&mut req),
            "D" => do_due(&mut req),
            "B" => do_backoff(&mut req),
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
