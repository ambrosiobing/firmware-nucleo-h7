//! The Rust codec as a filter, speaking the protocol the C++ variant already
//! speaks so that one parity test can drive both with the same code.
//!
//! One case per line on stdin, five integers:
//!
//! ```text
//! version flags sequence feature battery
//! ```
//!
//! One line per case on stdout: the five encoded bytes as uppercase hex, then
//! the round trip of those bytes back into fields.
//!
//! ```text
//! 2405FFFFE8 1 2 5 -1 40
//! ```
//!
//! Why a filter and not a test that reads `../vectors.json`. Parsing JSON here
//! would mean a dependency, or thirty lines that test nothing, and it would
//! limit the comparison to the six hand-computed vectors. As a filter the same
//! hundred thousand cases go through all four implementations, and the vectors
//! stay in the one file that no generator has ever touched.
//!
//! One process handles the whole run. A process per case would measure the
//! operating system instead of the codec, which is the same reason the C++
//! filter is shaped this way.

use std::io::{self, BufWriter, Read, Write};

use p09_payload::{decode, encode, Payload, PAYLOAD_BYTES};

fn main() -> io::Result<()> {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input)?;

    let stdout = io::stdout();
    let mut out = BufWriter::new(stdout.lock());

    for line in input.lines() {
        let numbers: Vec<&str> = line.split_whitespace().collect();
        if numbers.len() != 5 {
            continue; // blank or malformed line, skipped, as the C++ filter does
        }
        // Parsed as i64 and narrowed, so a case outside a field's range reaches
        // the codec and exercises the documented masking rather than being
        // rejected here. The C++ filter does the same with long long.
        //
        // Collecting into Option<Vec<_>> rather than looping: one unparsable
        // number has to skip the whole line, and a `continue` inside a loop over
        // the five numbers would skip only that number and encode a case the
        // caller never sent.
        let value: Vec<i64> = match numbers.iter().map(|t| t.parse::<i64>().ok()).collect() {
            Some(v) => v,
            None => continue,
        };
        let p = Payload {
            version: value[0] as u32,
            flags: value[1] as u32,
            sequence: value[2] as u32,
            feature: value[3] as i32,
            battery: value[4] as u32,
        };

        let mut buf = [0u8; PAYLOAD_BYTES];
        if encode(&mut buf, &p).is_none() {
            // Unreachable with a buffer of exactly PAYLOAD_BYTES, and reported
            // rather than ignored, because a filter that silently drops a line
            // would make the parity test fail on a line count with no reason.
            writeln!(out, "ENCODE_REFUSED")?;
            continue;
        }
        for byte in &buf {
            write!(out, "{:02X}", byte)?;
        }
        match decode(&buf) {
            Ok(d) => writeln!(
                out,
                " {} {} {} {} {}",
                d.version, d.flags, d.sequence, d.feature, d.battery
            )?,
            Err(e) => writeln!(out, " DECODE_REFUSED {:?}", e)?,
        }
    }
    out.flush()
}
