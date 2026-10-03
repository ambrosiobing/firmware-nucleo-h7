//! P08's Rust table as a filter, speaking the protocol the C++ filter speaks.
//!
//! One event sequence per line on stdin: the sample a caller would have set
//! before `BLOCK`, then the events by name.
//!
//! ```text
//! 5 TICK BLOCK FEATURE_DONE FRAME_READY TX_OK
//! ```
//!
//! One answer per line on stdout, naming every row the dispatch took, which is
//! the field that makes this a comparison of tables rather than of behaviour:
//!
//! ```text
//! state=IDLE rows=0,5,8,10,12 attempts=1 ok=1 dropped=0 seq=1 feature=5 frame=2001000528 unhandled=0 handled=5
//! ```
//!
//! `rows=-` means no event was handled, and `frame=-` that no frame was
//! encoded. Unhandled events are counted here from the `None` returns rather
//! than read out of the implementation, because the four keep that counter in
//! four places and counting the returns is the one way to ask all four the same
//! question.

use std::io::{self, BufWriter, Read, Write};

use p08_node_sm::{dispatch, event_from_name, state_name, Ctx};

fn main() -> io::Result<()> {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input)?;

    let stdout = io::stdout();
    let mut out = BufWriter::new(stdout.lock());

    for line in input.lines() {
        let mut fields = line.split_whitespace();
        let sample = match fields.next() {
            Some(text) => match text.parse::<i32>() {
                Ok(n) => n,
                Err(_) => {
                    writeln!(out, "BAD_SAMPLE {}", text)?;
                    continue;
                }
            },
            None => continue, // a blank line
        };

        let mut ctx = Ctx {
            pending_sample: sample,
            ..Default::default()
        };
        let mut rows: Vec<String> = Vec::new();
        let mut unhandled = 0u32;
        let mut handled = 0u32;
        let mut refused = false;

        for token in fields {
            let event = match event_from_name(token) {
                Some(e) => e,
                None => {
                    writeln!(out, "BAD_EVENT {}", token)?;
                    refused = true;
                    break;
                }
            };
            match dispatch(&mut ctx, event) {
                Some(row) => {
                    handled += 1;
                    rows.push(row.to_string());
                }
                None => {
                    unhandled += 1;
                    // Recorded as -1 so the answers line up with the events one
                    // for one, as the C++ filter does.
                    rows.push("-1".to_string());
                }
            }
        }
        if refused {
            continue;
        }

        let row_text = if rows.is_empty() {
            "-".to_string()
        } else {
            rows.join(",")
        };
        let frame_text = if ctx.frame_len == 0 {
            "-".to_string()
        } else {
            ctx.frame[..ctx.frame_len as usize]
                .iter()
                .map(|b| format!("{:02X}", b))
                .collect()
        };

        writeln!(
            out,
            "state={} rows={} attempts={} ok={} dropped={} seq={} feature={} frame={} unhandled={} handled={}",
            state_name(ctx.state),
            row_text,
            ctx.tx_attempts,
            ctx.cycles_ok,
            ctx.cycles_dropped,
            ctx.sequence,
            ctx.feature,
            frame_text,
            unhandled,
            handled
        )?;
    }
    out.flush()
}
