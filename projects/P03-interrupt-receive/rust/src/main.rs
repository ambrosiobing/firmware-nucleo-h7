//! P03's attribution as a filter, speaking the three verbs the C++ filter speaks.
//!
//! ```text
//! A <rate> <sent> <accepted> <overruns> <dropped>
//!      attribute that step and remember the row. Answers the nine fields, or
//!      "REFUSED overaccounted".
//! F    the lowest-rate row where the target itself lost a byte, over the rows
//!      remembered since the last R. "first_loss=<index>" or "first_loss=none".
//! R    forget the remembered rows. Answers "reset".
//! ```
//!
//! Both functions are in the protocol because both are part of the claim. The
//! per-step attribution decides what a row means; `first_loss` decides what a
//! ramp means, and it is the one that must skip bridge rows rather than naming
//! the lowest bridge failure as a target limit.

use std::io::{self, BufWriter, Read, Write};

use p03_attribute::{attribute, first_loss, Row, Step};

fn main() -> io::Result<()> {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input)?;

    let stdout = io::stdout();
    let mut out = BufWriter::new(stdout.lock());
    let mut rows: Vec<Row> = Vec::new();

    for line in input.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }

        match line.as_bytes()[0] {
            b'A' => {
                let numbers: Option<Vec<u32>> = line[1..]
                    .split_whitespace()
                    .map(|t| t.parse::<u32>().ok())
                    .collect();
                let numbers = match numbers {
                    Some(n) if n.len() == 5 => n,
                    _ => {
                        writeln!(out, "BAD_STEP")?;
                        continue;
                    }
                };
                let step = Step {
                    rate: numbers[0],
                    sent: numbers[1],
                    accepted: numbers[2],
                    overruns: numbers[3],
                    dropped: numbers[4],
                };
                match attribute(&step) {
                    Ok(row) => {
                        rows.push(row);
                        writeln!(
                            out,
                            "rate={} sent={} delivered={} overrun={} dropped={} \
                             bridge_lost={} target_lost={} reached={} verdict={}",
                            row.rate,
                            row.sent,
                            row.delivered,
                            row.overrun,
                            row.dropped,
                            row.bridge_lost,
                            row.target_lost,
                            row.reached,
                            row.verdict.name()
                        )?;
                    }
                    Err(_) => writeln!(out, "REFUSED overaccounted")?,
                }
            }

            b'F' => match first_loss(&rows) {
                Some(at) => writeln!(out, "first_loss={}", at)?,
                None => writeln!(out, "first_loss=none")?,
            },

            b'R' => {
                rows.clear();
                writeln!(out, "reset")?;
            }

            other => writeln!(out, "BAD_VERB {}", other as char)?,
        }
    }
    out.flush()
}
