//! P05's Rust framing as a filter, speaking the three verbs the C++ filter
//! speaks so that one parity test drives both with the same code.
//!
//! One request per line on stdin, one answer per line on stdout:
//!
//! ```text
//! C <hex>      the CRC-16 of those bytes, four uppercase hex digits
//! E <hex>      the frame for that payload, delimiter included, or REFUSED
//! D <hex>      a verdict and the payload as hex, or - when there is none
//! ```
//!
//! A blank hex field is a zero-length input. One process handles the whole
//! run, for the reason P09's filter gives: a process per case would measure the
//! operating system rather than the codec.

use std::io::{self, BufWriter, Read, Write};

use p05_frame::{crc16, frame_decode, frame_encode, verdict_name, Decoded};

/// Hex to bytes, or `None` on an odd length or a bad digit, so a malformed
/// request is reported rather than half decoded.
fn from_hex(text: &str) -> Option<Vec<u8>> {
    if text.len() % 2 != 0 {
        return None;
    }
    (0..text.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&text[i..i + 2], 16).ok())
        .collect()
}

fn to_hex(data: &[u8]) -> String {
    data.iter().map(|b| format!("{:02X}", b)).collect()
}

fn main() -> io::Result<()> {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input)?;

    let stdout = io::stdout();
    let mut out = BufWriter::new(stdout.lock());

    for line in input.lines() {
        let line = line.trim_end_matches(['\r', '\n']);
        if line.is_empty() {
            continue;
        }
        let (verb, hex) = match line.split_once(' ') {
            Some((v, h)) => (v, h.trim()),
            None => (line, ""),
        };
        let bytes = match from_hex(hex) {
            Some(b) => b,
            None => {
                writeln!(out, "BAD_HEX")?;
                continue;
            }
        };

        match verb {
            "C" => writeln!(out, "{:04X}", crc16(&bytes))?,

            "E" => {
                let mut frame = [0u8; 512];
                match frame_encode(&bytes, &mut frame) {
                    Some(m) => writeln!(out, "{}", to_hex(&frame[..m]))?,
                    None => writeln!(out, "REFUSED")?,
                }
            }

            "D" => {
                let mut payload = [0u8; 512];
                let r = frame_decode(&bytes, &mut payload);
                match r {
                    Ok(Decoded::Payload(n)) => {
                        writeln!(out, "{} {}", verdict_name(r), to_hex(&payload[..n]))?
                    }
                    _ => writeln!(out, "{} -", verdict_name(r))?,
                }
            }

            _ => writeln!(out, "BAD_VERB")?,
        }
    }
    out.flush()
}
