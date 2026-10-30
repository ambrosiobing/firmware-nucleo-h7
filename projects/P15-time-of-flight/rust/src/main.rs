//! P15's filter: one request per line on stdin, one answer per line on stdout.
//!
//! One verb, because this project is one state machine fed a sequence:
//!
//! ```text
//! T <far> <enter> <leave> <confirm_n> <confirm_m> <n> <distance>*n
//!     -> frames=N changes=N ready=0|1 states=clear,approaching,..
//!     -> REFUSED policy
//! ```
//!
//! THE POLICY TRAVELS IN EVERY REQUEST, because it is a parameter in all four
//! implementations: the chapter's own note on figure 15.4 calls its three
//! thresholds "a starting point measured against a real target rather than
//! constants to be trusted", and six of the oracle's nineteen cases override
//! them. A filter holding a policy between lines would answer a request that
//! never named one.
//!
//! THE STATE AFTER EVERY FRAME IS REPORTED, as names, in one comma separated
//! token. The oracle states the whole sequence rather than the final state,
//! because a state machine that reaches the right end through the wrong middle
//! is wrong, and three of the cases exist to pin a middle: a window cleared on
//! an edge, a target that comes back while leaving, and a hover between the two
//! thresholds that never confirms anything.
//!
//! The counters come back too. `changes` is the figure the chapter says a policy
//! is judged by, and the parity test checks it against the number of
//! transitions in the oracle's own sequence of states, which is a comparison
//! neither implementation can satisfy by accident.
//!
//! A refusal is answered for the whole request rather than per frame: the C
//! validates the policy on every update, so a policy that is refused is refused
//! on the first frame, and no state is produced at all.
//!
//! Every value here is an integer, so nothing in this protocol rounds.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p15_zonetrack::{Policy, Tracker};

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

    fn u8(&mut self) -> Option<u8> {
        self.token()?.parse::<u8>().ok()
    }

    fn u16(&mut self) -> Option<u16> {
        self.token()?.parse::<u16>().ok()
    }

    fn usize(&mut self) -> Option<usize> {
        self.token()?.parse::<usize>().ok()
    }

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

fn do_track(req: &mut Request) -> Option<String> {
    let policy = Policy {
        far_mm: req.u16()?,
        enter_mm: req.u16()?,
        leave_mm: req.u16()?,
        confirm_n: req.u8()?,
        confirm_m: req.u8()?,
    };
    let n = req.usize()?;
    let mut distances = Vec::with_capacity(n);
    for _ in 0..n {
        distances.push(req.u16()?);
    }

    let mut tracker = Tracker::new();
    let mut names: Vec<&'static str> = Vec::with_capacity(n);
    for d in &distances {
        match tracker.update(&policy, *d) {
            Ok(state) => names.push(state.name()),
            Err(e) => return Some(format!("REFUSED {}", e.name())),
        }
    }
    Some(format!(
        "frames={} changes={} ready={} states={}",
        tracker.frames,
        tracker.changes,
        u8::from(tracker.ready),
        names.join(",")
    ))
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
        if verb != "T" {
            let _ = writeln!(out, "BAD_VERB {verb}");
            return ExitCode::FAILURE;
        }
        let mut req = Request::new(rest);
        match do_track(&mut req) {
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
