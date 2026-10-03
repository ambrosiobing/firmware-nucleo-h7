//! P02's Rust ring as a filter, speaking the four verbs the C++ filter speaks.
//!
//! One operation per line on stdin, one answer per line on stdout:
//!
//! ```text
//! I <mode>   a fresh ring in that ordering mode, 0 to 3. "init mode=N cap=256"
//! P <hex>    put that byte. "put=1 used=N" or "put=0 drops=N"
//! G          get. "get=1 byte=XX used=N" or "get=0"
//! S          state. "used=N drops=N head=N tail=N"
//! ```
//!
//! One operation per line rather than a whole sequence per line, the opposite of
//! the P08 filter and for a reason: P02's comparison runs to hundreds of
//! thousands of operations, and a line that long is what broke the P08 filter.
//!
//! All four modes are held at once because `Ring<0>` through `Ring<3>` are four
//! distinct types, so a mode cannot be a variable. That is the same shape the
//! C++ filter has for the same reason, and it is what the C avoids by building
//! four separate programs.

use std::io::{self, BufWriter, Read, Write};

use p02_ring::{Ring, RING_SIZE};

/// Which ring is live, and the state of all four. A mode is a type here, so
/// this cannot be a single variable the way it could in C.
struct Rings {
    live: u8,
    r0: Ring<0>,
    r1: Ring<1>,
    r2: Ring<2>,
    r3: Ring<3>,
}

impl Rings {
    fn new() -> Self {
        Self {
            // The C's default mode, so a run that forgets to send I matches it.
            live: 2,
            r0: Ring::new(),
            r1: Ring::new(),
            r2: Ring::new(),
            r3: Ring::new(),
        }
    }

    fn init(&mut self, mode: u8) {
        match mode {
            0 => self.r0.init(),
            1 => self.r1.init(),
            2 => self.r2.init(),
            _ => self.r3.init(),
        }
        self.live = mode;
    }

    fn put(&mut self, byte: u8) -> bool {
        match self.live {
            0 => self.r0.put(byte),
            1 => self.r1.put(byte),
            2 => self.r2.put(byte),
            _ => self.r3.put(byte),
        }
    }

    fn get(&mut self) -> Option<u8> {
        match self.live {
            0 => self.r0.get(),
            1 => self.r1.get(),
            2 => self.r2.get(),
            _ => self.r3.get(),
        }
    }

    /// used, drops, head, tail.
    fn state(&self) -> (u32, u32, u32, u32) {
        match self.live {
            0 => (
                self.r0.used(),
                self.r0.drops(),
                self.r0.head(),
                self.r0.tail(),
            ),
            1 => (
                self.r1.used(),
                self.r1.drops(),
                self.r1.head(),
                self.r1.tail(),
            ),
            2 => (
                self.r2.used(),
                self.r2.drops(),
                self.r2.head(),
                self.r2.tail(),
            ),
            _ => (
                self.r3.used(),
                self.r3.drops(),
                self.r3.head(),
                self.r3.tail(),
            ),
        }
    }
}

fn main() -> io::Result<()> {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input)?;

    let stdout = io::stdout();
    let mut out = BufWriter::new(stdout.lock());
    let mut rings = Rings::new();

    for line in input.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        let verb = line.as_bytes()[0];
        let rest = line[1..].trim();

        match verb {
            b'I' => match rest.parse::<u8>() {
                Ok(mode) if mode <= 3 => {
                    rings.init(mode);
                    writeln!(out, "init mode={} cap={}", mode, RING_SIZE)?;
                }
                _ => writeln!(out, "BAD_MODE {}", rest)?,
            },

            b'P' => match u8::from_str_radix(rest, 16) {
                Ok(byte) => {
                    if rings.put(byte) {
                        writeln!(out, "put=1 used={}", rings.state().0)?;
                    } else {
                        writeln!(out, "put=0 drops={}", rings.state().1)?;
                    }
                }
                Err(_) => writeln!(out, "BAD_HEX {}", rest)?,
            },

            b'G' => match rings.get() {
                Some(byte) => writeln!(out, "get=1 byte={:02X} used={}", byte, rings.state().0)?,
                None => writeln!(out, "get=0")?,
            },

            b'S' => {
                let (used, drops, head, tail) = rings.state();
                writeln!(
                    out,
                    "used={} drops={} head={} tail={}",
                    used, drops, head, tail
                )?;
            }

            _ => writeln!(out, "BAD_VERB {}", verb as char)?,
        }
    }
    out.flush()
}
