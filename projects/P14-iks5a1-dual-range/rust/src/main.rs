//! P14's filter: one request per line on stdin, one answer per line on stdout.
//!
//! Six verbs, one per function the oracle exercises:
//!
//! ```text
//! H <n> <byte>*n                     -> tick=.. pre=.. post=.. baro=..
//!                                       lo=.. hi=.. range=.. flags=..
//! M <n> <byte>*n <table> <counts> <fine>   -> mg=..
//! P <n> <byte>*n <table> <counts>          -> hpa=..
//! A <p_hpa> <p0_hpa>                     -> m=..
//! R <lsb_per_hpa> <p_hpa> <p0_hpa>       -> m=..
//! D <samples> <bytes_per_sample> <overhead> <bus_hz>  -> ms=..
//! ```
//!
//! and any of them can answer `REFUSED <name>` with one of the six names the C
//! uses.
//!
//! THE BYTE COUNT TRAVELS WITH THE BYTES, and one oracle case is the whole
//! reason: "a buffer shorter than the header" is THIRTEEN bytes, and its point
//! is that the parse refuses before reading a field. This protocol carried
//! exactly fourteen until Thursday 29 October 2026, when the laptop with cargo
//! answered BAD_REQUEST for that case: the filter could not carry it, and a
//! filter that cannot carry a case cannot test it. That rule is written in
//! P13's own filter two projects away, in the same words, about a different
//! parameter.
//!
//! `<table>` IS FIFTEEN TOKENS AND TRAVELS IN EVERY REQUEST THAT NEEDS IT: five
//! fine sensitivities, five coarse ones, two barometer sensitivities, then the
//! three valid masks. **It is not held between lines**, and that is deliberate
//! rather than frugal. A filter that remembered a table set by an earlier
//! request would answer a request that never named one, and the thing being
//! tested here is precisely that a decoder uses the record's own codes against
//! a table it was given rather than one it assumed.
//!
//! The sensitivities are the caller's because this bench holds neither sensor's
//! datasheet, so a table compiled in would be an invention; the reasoning is in
//! the crate comment.
//!
//! Every floating point value crosses exactly: parsed with
//! `str::parse::<f64>`, which is correctly rounded, and printed with `{:?}`,
//! the shortest form that reads back as the same bits. The conversions are
//! compared exactly, because the oracle's table is powers of two; the altitudes
//! raise a ratio to a fractional power, which is a library call in every
//! language, so those are compared to a relative 1e-12 and nothing tighter.

use std::io::{self, Read, Write};
use std::process::ExitCode;

// An alias rather than a list of eleven names: rustfmt's default layout fills
// a brace group to the line width, and hand-wrapping eleven items is a guess at
// where it would break. Qualifying each call also says which crate answers it.
use p14_recdecode as rec;

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

    fn u32(&mut self) -> Option<u32> {
        self.token()?.parse::<u32>().ok()
    }

    fn i16(&mut self) -> Option<i16> {
        self.token()?.parse::<i16>().ok()
    }

    fn i32(&mut self) -> Option<i32> {
        self.token()?.parse::<i32>().ok()
    }

    fn f64(&mut self) -> Option<f64> {
        self.token()?.parse::<f64>().ok()
    }

    fn usize(&mut self) -> Option<usize> {
        self.token()?.parse::<usize>().ok()
    }

    /// A count and then that many header bytes, which is how a buffer shorter
    /// than the header crosses the protocol.
    fn header_bytes(&mut self) -> Option<Vec<u8>> {
        let want = self.usize()?;
        if want > 4 * rec::HDR_BYTES {
            return None;
        }
        let mut out = Vec::with_capacity(want);
        for _ in 0..want {
            out.push(self.u8()?);
        }
        Some(out)
    }

    /// The fifteen table tokens, in the order the crate declares the fields.
    fn table(&mut self) -> Option<rec::Table> {
        let mut lo = [0.0f64; rec::FS_CODES];
        for v in &mut lo {
            *v = self.f64()?;
        }
        let mut hi = [0.0f64; rec::FS_CODES];
        for v in &mut hi {
            *v = self.f64()?;
        }
        let mut baro = [0.0f64; rec::BARO_CODES];
        for v in &mut baro {
            *v = self.f64()?;
        }
        Some(rec::Table {
            lo_mg_per_lsb: lo,
            hi_mg_per_lsb: hi,
            baro_lsb_per_hpa: baro,
            lo_valid: self.u8()?,
            hi_valid: self.u8()?,
            baro_valid: self.u8()?,
        })
    }

    fn exhausted(&self) -> bool {
        self.at >= self.tokens.len()
    }
}

fn refused(name: &str) -> String {
    format!("REFUSED {name}")
}

fn header_of(req: &mut Request) -> Option<Result<rec::Header, String>> {
    let bytes = req.header_bytes()?;
    Some(match rec::parse_header(&bytes) {
        Ok(h) => Ok(h),
        Err(e) => Err(refused(e.name())),
    })
}

fn do_header(req: &mut Request) -> Option<String> {
    Some(match header_of(req)? {
        Err(text) => text,
        Ok(h) => format!(
            "tick={} pre={} post={} baro={} lo={} hi={} range={} flags={}",
            h.tick_event, h.n_pre, h.n_post, h.n_baro, h.lo_fs_g, h.hi_fs_g, h.baro_range, h.flags
        ),
    })
}

fn do_mg(req: &mut Request) -> Option<String> {
    let header = header_of(req)?;
    let table = req.table()?;
    let counts = req.i16()?;
    let fine = req.u8()? != 0;
    let hdr = match header {
        Err(text) => return Some(text),
        Ok(h) => h,
    };
    Some(match rec::counts_to_mg(&hdr, &table, counts, fine) {
        Ok(mg) => format!("mg={mg:?}"),
        Err(e) => refused(e.name()),
    })
}

fn do_hpa(req: &mut Request) -> Option<String> {
    let header = header_of(req)?;
    let table = req.table()?;
    let counts = req.i32()?;
    let hdr = match header {
        Err(text) => return Some(text),
        Ok(h) => h,
    };
    Some(match rec::counts_to_hpa(&hdr, &table, counts) {
        Ok(hpa) => format!("hpa={hpa:?}"),
        Err(e) => refused(e.name()),
    })
}

fn do_altitude(req: &mut Request) -> Option<String> {
    let p = req.f64()?;
    let p0 = req.f64()?;
    Some(match rec::altitude_m(p, p0) {
        Ok(m) => format!("m={m:?}"),
        Err(e) => refused(e.name()),
    })
}

fn do_resolution(req: &mut Request) -> Option<String> {
    let lsb = req.f64()?;
    let p = req.f64()?;
    let p0 = req.f64()?;
    Some(match rec::altitude_resolution_m(lsb, p, p0) {
        Ok(m) => format!("m={m:?}"),
        Err(e) => refused(e.name()),
    })
}

fn do_drain(req: &mut Request) -> Option<String> {
    let samples = req.u32()?;
    let per_sample = req.u32()?;
    let overhead = req.u32()?;
    let bus_hz = req.f64()?;
    Some(match rec::drain_ms(samples, per_sample, overhead, bus_hz) {
        Ok(ms) => format!("ms={ms:?}"),
        Err(e) => refused(e.name()),
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
            "H" => do_header(&mut req),
            "M" => do_mg(&mut req),
            "P" => do_hpa(&mut req),
            "A" => do_altitude(&mut req),
            "R" => do_resolution(&mut req),
            "D" => do_drain(&mut req),
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
