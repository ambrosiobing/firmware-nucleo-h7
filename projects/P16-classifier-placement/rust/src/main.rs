//! P16's filter: one request per line on stdin, one answer per line on stdout.
//!
//! Two verbs, each taking its counts before its data so the parser never
//! guesses where a list ends:
//!
//! ```text
//! F <n> <axes> <n*axes samples>
//!     -> mean=a,b,c rms=a,b,c p2p=a,b,c vector=v0,..,v8
//! C <nodes> <depth_max> <nfeat> <5 values per node> <nfeat features>
//!     -> class=k
//! ```
//!
//! A node is five values in the header's order: feature, leaf, threshold, left,
//! right. A refusal is `REFUSED <name>` with the same six names the C's six
//! error codes carry.
//!
//! One window is 384 integers, so an F request is a long line. It is read with
//! the whole of stdin and split by lines, after P08's filter read into a fixed
//! buffer, split a twenty-one-thousand-character request into several, and
//! answered twenty requests with twenty five answers.
//!
//! A request with anything left over is refused, the lesson P18's filter was
//! given on Monday 26 October 2026: ignoring a remainder lets a caller send one
//! thing and be answered about another, and it caught a real defect there.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p16_winfeat::{classify, vector, window, Node};

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

    fn i16(&mut self) -> Option<i16> {
        self.token()?.parse::<i16>().ok()
    }

    fn u8(&mut self) -> Option<u8> {
        self.token()?.parse::<u8>().ok()
    }

    fn usize(&mut self) -> Option<usize> {
        self.token()?.parse::<usize>().ok()
    }

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

fn joined(values: &[i16]) -> String {
    let mut out = String::new();
    for (i, v) in values.iter().enumerate() {
        if i > 0 {
            out.push(',');
        }
        out.push_str(&v.to_string());
    }
    out
}

fn do_features(req: &mut Request) -> Option<String> {
    let n = req.usize()?;
    let axes = req.usize()?;
    // The counts come first and the samples are counted against them, so a
    // request that claims more than it carries is malformed rather than
    // refused: the implementation never sees it.
    let mut samples = Vec::with_capacity(n.saturating_mul(axes));
    for _ in 0..n.saturating_mul(axes) {
        samples.push(req.i16()?);
    }
    Some(match window(&samples, n, axes) {
        Ok(f) => format!(
            "mean={} rms={} p2p={} vector={}",
            joined(&f.mean),
            joined(&f.rms),
            joined(&f.p2p),
            joined(&vector(&f))
        ),
        Err(e) => format!("REFUSED {}", e.name()),
    })
}

fn do_classify(req: &mut Request) -> Option<String> {
    let count = req.usize()?;
    let depth_max = req.usize()?;
    let nfeat = req.usize()?;
    let mut nodes = Vec::with_capacity(count);
    for _ in 0..count {
        let feature = req.u8()?;
        let leaf = req.u8()?;
        let threshold = req.i16()?;
        let left = req.u8()?;
        let right = req.u8()?;
        nodes.push(Node {
            feature,
            leaf: leaf != 0,
            threshold,
            left,
            right,
        });
    }
    let mut features = Vec::with_capacity(nfeat);
    for _ in 0..nfeat {
        features.push(req.i16()?);
    }
    Some(match classify(&nodes, depth_max, &features) {
        Ok(k) => format!("class={k}"),
        Err(e) => format!("REFUSED {}", e.name()),
    })
}

fn answer(verb: &str, req: &mut Request) -> Option<String> {
    match verb {
        "F" => do_features(req),
        "C" => do_classify(req),
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
        if !matches!(verb, "F" | "C") {
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
