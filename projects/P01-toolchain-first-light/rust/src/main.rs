//! P01's filter: one request per line on stdin, one answer per line on stdout.
//!
//! The protocol is `../../PROTOCOL.md`, and the C++ filter beside this one
//! speaks it too, which is what lets one parity test drive both with the same
//! code. Two verbs:
//!
//! ```text
//! D <cr> <cfgr> <pllckselr> <pllcfgr> <pll1divr> <cdcfgr1> <cdcfgr2> <hsi> <hse>
//! B <reported> <true>
//! ```
//!
//! It reads all of stdin and then splits on newlines rather than reading a line
//! at a time into a fixed buffer. That is a lesson and not a preference: P08's
//! C++ filter used a 4096-byte buffer, met a request of twenty one thousand
//! characters on Saturday 3 October 2026, and answered twenty requests with
//! twenty five answers, having split one long line into several that each parsed
//! into something. A filter that quietly turns one request into several is the
//! worst failure available to it, because the parity test then reports a
//! disagreement about the clock tree when the fault was the parser.
//!
//! A malformed request is refused loudly and the process exits non-zero. It
//! never guesses and it never silently skips a line, because a skipped line
//! misaligns every answer after it.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p01_clocktree::{bias, decode, Regs};

/// Every field in this protocol is a decimal integer that fits in `u32`.
/// `str::parse` rejects a value that does not fit rather than saturating, which
/// is what makes an out-of-range register a refusal here instead of a plausible
/// number. The C filter has to reach for `strtoull` and a range check to get the
/// same behaviour, because `strtoul` on a 32-bit `long` saturates and reports
/// success.
fn parse_u32(text: &str) -> Option<u32> {
    if text.is_empty() {
        return None;
    }
    text.parse::<u32>().ok()
}

fn answer_decode(fields: &[&str]) -> Option<String> {
    if fields.len() != 10 {
        return None;
    }
    let mut v = [0u32; 9];
    for (slot, text) in v.iter_mut().zip(&fields[1..]) {
        *slot = parse_u32(text)?;
    }
    let regs = Regs {
        cr: v[0],
        cfgr: v[1],
        pllckselr: v[2],
        pllcfgr: v[3],
        pll1divr: v[4],
        cdcfgr1: v[5],
        cdcfgr2: v[6],
    };
    let tree = decode(&regs, v[7], v[8]);
    Some(format!(
        "{} {} {} {} {} {}",
        tree.sys_hz,
        tree.core_hz,
        tree.ahb_hz,
        tree.pclk1_hz,
        tree.pclk2_hz,
        tree.refusal.text()
    ))
}

fn answer_bias(fields: &[&str]) -> Option<String> {
    if fields.len() != 3 {
        return None;
    }
    let reported = parse_u32(fields[1])?;
    let truth = parse_u32(fields[2])?;
    let b = bias(reported, truth);
    Some(format!(
        "{} {} {} {}",
        u8::from(b.ok),
        b.frequency_ppm,
        b.duration_ppm,
        b.delay_ppm
    ))
}

fn main() -> ExitCode {
    let mut all = String::new();
    if io::stdin().read_to_string(&mut all).is_err() {
        eprintln!("p01-filter: stdin is not valid UTF-8");
        return ExitCode::FAILURE;
    }

    let stdout = io::stdout();
    let mut out = io::BufWriter::new(stdout.lock());

    for line in all.split('\n') {
        // Only the trailing fragment after the final newline is empty, and
        // split('\n') puts it last, so skipping it cannot misalign anything. A
        // blank line in the middle would be a request with no verb and is
        // refused by the match below.
        if line.is_empty() {
            continue;
        }
        let fields: Vec<&str> = line.split(' ').collect();
        let answer = match fields.first() {
            Some(&"D") => answer_decode(&fields),
            Some(&"B") => answer_bias(&fields),
            _ => None,
        };
        match answer {
            Some(text) => {
                if writeln!(out, "{text}").is_err() {
                    return ExitCode::FAILURE;
                }
            }
            None => {
                let _ = writeln!(out, "BAD REQUEST: {line}");
                let _ = out.flush();
                return ExitCode::FAILURE;
            }
        }
    }
    if out.flush().is_err() {
        return ExitCode::FAILURE;
    }
    ExitCode::SUCCESS
}
