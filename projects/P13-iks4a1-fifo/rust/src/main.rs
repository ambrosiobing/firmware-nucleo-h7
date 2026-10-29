//! P13's filter: one request per line on stdin, one answer per line on stdout.
//!
//! Three verbs, one per piece of chapter 13 that needs no shield:
//!
//! ```text
//! G <expect> <cnt> <cnt_bits>              -> gap=N
//! D <word_bytes> <tag_shift> <cnt_shift> <cnt_bits> <known_tags>
//!   <words> <n> <first byte>*n
//!       -> words=N unknown=N drops=N gaps=N tags=a,b,..  (32 counts)
//!       -> REFUSED format | words | argument
//! B <watermark> <sources> <word_bytes> <cnt_bits> <bus_hz> <decode_us>
//!   <rate>*8
//!       -> entries=.. fill=.. chapter=.. drain=.. ratio=.. gap=N ok=0|1
//!       -> REFUSED sources | format | words | rate
//! ```
//!
//! THE WORD COUNT AND THE NUMBER OF SUPPLIED BYTES ARE SEPARATE TOKENS, and one
//! oracle case is the whole reason. "More words than the landing buffer can
//! hold" passes a level of 1025 with no bytes at all, because the refusal is
//! decided before anything is read. A protocol that derived the level from the
//! bytes could not carry that case, and a filter that cannot carry a case
//! cannot test it.
//!
//! THE FILTER PADS THE BLOCK ITSELF, which is what the C's parity test does
//! too: each word is its tag byte followed by `word_bytes - 1` zeros, so the
//! request carries one byte per word rather than seven. The data bytes are not
//! read by any of this arithmetic, and sending them would make a request for a
//! 1024-word drain seven thousand tokens long.
//!
//! ALL THIRTY-TWO TAG COUNTS ARE PRINTED, zeros included, as one comma
//! separated token. The oracle states only the non-zero ones, so printing the
//! whole array is what lets a test assert that every other slot stayed at zero,
//! and an unknown tag is counted separately rather than indexing into nothing.
//!
//! Every floating point value crosses exactly: parsed with `str::parse::<f64>`,
//! which is correctly rounded, and printed with `{:?}`, the shortest form that
//! reads back as the same bits. The budget figures are compared to a relative
//! 1e-12 by the oracle, the decode figures exactly.
//!
//! The library this drives is `no_std`; this binary is not, because it reads
//! stdin and writes stdout.

use std::io::{self, Read, Write};
use std::process::ExitCode;

use p13_fifodecode::{budget, cnt_gap, decode, Format, Spec, State, MAX_SOURCES};

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

fn do_gap(req: &mut Request) -> Option<String> {
    let expect = req.u8()?;
    let cnt = req.u8()?;
    let cnt_bits = req.u8()?;
    Some(format!("gap={}", cnt_gap(expect, cnt, cnt_bits)))
}

fn do_decode(req: &mut Request) -> Option<String> {
    let fmt = Format {
        word_bytes: req.u8()?,
        tag_shift: req.u8()?,
        cnt_shift: req.u8()?,
        cnt_bits: req.u8()?,
        known_tags: req.u32()?,
    };
    let words = req.u16()?;
    let supplied = req.usize()?;

    // One byte per word in the request, padded here to the word length, which
    // is what the C's parity test does on its side of the same oracle.
    let stride = usize::from(fmt.word_bytes.max(1));
    let mut buf = vec![0u8; supplied * stride];
    for i in 0..supplied {
        buf[i * stride] = req.u8()?;
    }

    let mut state = State::new();
    match decode(&fmt, &buf, words, &mut state) {
        Err(e) => Some(format!("REFUSED {}", e.name())),
        Ok(()) => {
            let counts: Vec<String> = state.per_tag.iter().map(|n| n.to_string()).collect();
            let tags = counts.join(",");
            Some(format!(
                "words={} unknown={} drops={} gaps={} tags={}",
                state.words, state.unknown_tag, state.drops, state.gaps, tags
            ))
        }
    }
}

fn do_budget(req: &mut Request) -> Option<String> {
    let watermark_words = req.u16()?;
    let sources = req.u8()?;
    let word_bytes = req.u8()?;
    let cnt_bits = req.u8()?;
    let bus_hz = req.f64()?;
    let decode_us_per_word = req.f64()?;
    let mut batch_hz = [0.0f64; MAX_SOURCES];
    for r in &mut batch_hz {
        *r = req.f64()?;
    }
    let spec = Spec {
        watermark_words,
        sources,
        word_bytes,
        cnt_bits,
        batch_hz,
        bus_hz,
        decode_us_per_word,
    };

    Some(match budget(&spec) {
        Err(e) => format!("REFUSED {}", e.name()),
        Ok(b) => format!(
            "entries={:?} fill={:?} chapter={:?} drain={:?} ratio={:?} \
             gap={} ok={}",
            b.entries_per_s,
            b.fill_ms,
            b.chapter_fill_ms,
            b.drain_us,
            b.ratio,
            b.undetectable_gap,
            u8::from(b.ratio_ok)
        ),
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
            "G" => do_gap(&mut req),
            "D" => do_decode(&mut req),
            "B" => do_budget(&mut req),
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
