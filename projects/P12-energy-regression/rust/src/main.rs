//! P12's filter: one case per line on stdin, one answer per line on stdout.
//!
//! The protocol is `../../PROTOCOL.md`, and the C++ filter beside this one
//! speaks it too, which is what lets one parity test drive both with the same
//! code. Two verbs:
//!
//! ```text
//! S <measured> <budgets>
//! C <tolerance> <build> <total> <phases> <baseline>
//! ```
//!
//! It reads all of stdin and then splits on newlines rather than reading a line
//! at a time into a fixed buffer. That is a lesson rather than a preference:
//! P08's C++ filter used a 4096-byte buffer, met a request of twenty one
//! thousand characters on Saturday 3 October 2026, and answered twenty requests
//! with twenty five answers, because it had split one long line into several and
//! each fragment parsed into something. A filter that quietly turns one request
//! into several is the worst failure available to it, because the parity test
//! then compares misaligned answers and reports a disagreement about the gate.
//!
//! A malformed request is refused loudly and the process exits non-zero. It
//! never guesses, and it never silently skips a line, because a skipped line
//! also misaligns every answer after it.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p12_gates::{charge_answer, size_answer, PhaseMap, SizeMap, Sizes};

/// `-` is absent throughout the protocol, which is a real state in both gates
/// and not a formatting convenience: a build that reported no `static_ram` is a
/// different case from one that reported zero.
fn absent(field: &str) -> bool {
    field == "-"
}

fn parse_sizes(field: &str) -> Option<SizeMap> {
    let mut out = SizeMap::new();
    if absent(field) {
        return Some(out);
    }
    for item in field.split(',') {
        let parts: Vec<&str> = item.split(':').collect();
        if parts.len() != 3 || parts[0].is_empty() {
            return None;
        }
        // Built in one expression rather than default-then-assign, which clippy
        // refuses as field_reassign_with_default and which caught P08's crate on
        // Saturday 3 October 2026.
        let sizes = Sizes {
            flash: if absent(parts[1]) {
                None
            } else {
                Some(parts[1].parse::<i64>().ok()?)
            },
            static_ram: if absent(parts[2]) {
                None
            } else {
                Some(parts[2].parse::<i64>().ok()?)
            },
        };
        out.insert(parts[0].to_string(), sizes);
    }
    Some(out)
}

fn parse_phases(field: &str) -> Option<PhaseMap> {
    let mut out = PhaseMap::new();
    if absent(field) {
        return Some(out);
    }
    for item in field.split(',') {
        let parts: Vec<&str> = item.split(':').collect();
        if parts.len() != 2 || parts[0].is_empty() {
            return None;
        }
        out.insert(parts[0].to_string(), parts[1].parse::<f64>().ok()?);
    }
    Some(out)
}

fn refuse(out: &mut impl Write, why: &str) -> ExitCode {
    let _ = writeln!(out, "REFUSED {}", why);
    let _ = out.flush();
    ExitCode::from(2)
}

fn main() -> ExitCode {
    let mut text = String::new();
    if io::stdin().read_to_string(&mut text).is_err() {
        let mut stdout = io::stdout();
        return refuse(&mut stdout, "stdin_not_utf8");
    }

    let mut stdout = io::stdout();
    let mut answers = String::new();

    for raw in text.lines() {
        let line = raw.trim_end();
        if line.is_empty() {
            continue;
        }
        let fields: Vec<&str> = line.split_whitespace().collect();

        match fields.first().copied() {
            Some("S") => {
                if fields.len() != 3 {
                    return refuse(&mut stdout, "malformed_size_request");
                }
                let measured = match parse_sizes(fields[1]) {
                    Some(m) => m,
                    None => return refuse(&mut stdout, "unparsable_sizes"),
                };
                let budgets = match parse_sizes(fields[2]) {
                    Some(b) => b,
                    None => return refuse(&mut stdout, "unparsable_sizes"),
                };
                answers.push_str(&size_answer(&measured, &budgets));
                answers.push('\n');
            }
            Some("C") => {
                if fields.len() != 6 {
                    return refuse(&mut stdout, "malformed_charge_request");
                }
                let tolerance = match fields[1].parse::<f64>() {
                    Ok(t) => t,
                    Err(_) => return refuse(&mut stdout, "unparsable_tolerance"),
                };
                let build = if absent(fields[2]) { "" } else { fields[2] };
                let total = if absent(fields[3]) {
                    None
                } else {
                    match fields[3].parse::<f64>() {
                        Ok(t) => Some(t),
                        Err(_) => return refuse(&mut stdout, "unparsable_total"),
                    }
                };
                let phases = match parse_phases(fields[4]) {
                    Some(p) => p,
                    None => return refuse(&mut stdout, "unparsable_phases"),
                };
                let baseline = match parse_phases(fields[5]) {
                    Some(b) => b,
                    None => return refuse(&mut stdout, "unparsable_phases"),
                };
                answers.push_str(&charge_answer(build, &phases, total, &baseline, tolerance));
                answers.push('\n');
            }
            _ => return refuse(&mut stdout, "unknown_verb"),
        }
    }

    if stdout.write_all(answers.as_bytes()).is_err() || stdout.flush().is_err() {
        return ExitCode::from(2);
    }
    ExitCode::SUCCESS
}
