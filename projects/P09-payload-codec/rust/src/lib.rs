//! P09's 40-bit payload codec in Rust: the same layout, the same vectors.
//!
//! `no_std` except under `cargo test`, where the harness needs the standard
//! library. Nothing in the codec itself allocates, panics on valid input, or
//! touches a peripheral, which is what lets the same source serve the host
//! parity run and, later, the board image.
//!
//! `docs/bitorder.md` is normative for everything here, and `src/fields.rs` is
//! generated from `../fields.py` by `python/tools/gen_codec.py`, so the widths
//! and offsets cannot drift from the ones the C and Python sides use.
//!
//! **What this file is for, beyond having a fourth implementation.** The C
//! version is written around three defects the language makes possible, and it
//! says so in its comments: shifting a negative `int32_t` right is
//! implementation defined, converting an out-of-range unsigned value to a
//! signed type was implementation defined before C23, and an unsigned
//! subtraction that wraps hides the problem rather than reporting it. None of
//! the three is expressible here. Rust defines `>>` on a signed integer to be
//! an arithmetic shift, defines `as` between integer types to be a wrapping
//! two's complement conversion, and makes an overflowing subtraction a panic in
//! debug and a defined wrap in release. So the careful formulation the C needs
//! is not a formulation this needs, and the honest report is that the hazard is
//! absent rather than avoided.
//!
//! That is the finding worth printing in chapter 9, and it is the reason this
//! codec is still written out by hand in both languages rather than generated:
//! the comparison is only meaningful if both are idiomatic for their language.

#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]

pub mod fields;

use fields::{
    M_BATTERY, M_FEATURE, M_FLAGS, M_SEQUENCE, M_VERSION, O_BATTERY, O_FEATURE, O_FLAGS,
    O_SEQUENCE, O_VERSION, W_BATTERY, W_FEATURE, W_FLAGS, W_SEQUENCE, W_VERSION,
};

// Re-exported at the crate root so a caller needs one import rather than two,
// and so the filter and any future board image name the same two constants the
// C side names as PAYLOAD_BITS and PAYLOAD_BYTES.
pub use fields::{PAYLOAD_BITS, PAYLOAD_BYTES};

/// One decoded payload. Field for field the same as `payload_t` in
/// `c/payload/payload.h`, in the same order, with the same signedness.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Payload {
    /// format version, `W_VERSION` bits, unsigned
    pub version: u32,
    /// `W_FLAGS` bits, unsigned
    pub flags: u32,
    /// `W_SEQUENCE` bits, unsigned, wraps
    pub sequence: u32,
    /// `W_FEATURE` bits, two's complement
    pub feature: i32,
    /// `W_BATTERY` bits, unsigned
    pub battery: u32,
}

/// Why a decode refused. An encode has only one way to fail and reports it as
/// `None`, which is why there is no error type on that side.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DecodeError {
    /// The slice was not exactly `PAYLOAD_BYTES` long. A codec that accepted a
    /// longer slice would silently accept a frame with a trailing byte, which
    /// is the defect the C side guards with its `len != PAYLOAD_BYTES` test.
    WrongLength { got: usize },
}

struct Writer<'a> {
    buf: &'a mut [u8],
    bit: usize,
}

impl Writer<'_> {
    /// Most significant bit of the field first, no padding between fields.
    /// Written as a bit loop rather than as shifts into a `u64`, to stay the
    /// same algorithm the C and the Python implement, so a disagreement between
    /// the four is a disagreement about the layout and never about the method.
    fn put(&mut self, value: u32, width: u32) -> Option<()> {
        if self.bit + width as usize > self.buf.len() * 8 {
            return None;
        }
        for i in 0..width {
            let src = width - 1 - i; // MSB of the field first
            let dst = self.bit + i as usize;
            let bit = (value >> src) & 1;
            let mask = 0x80u8 >> (dst % 8);
            if bit == 1 {
                self.buf[dst / 8] |= mask;
            } else {
                self.buf[dst / 8] &= !mask;
            }
        }
        self.bit += width as usize;
        Some(())
    }
}

/// Encode into `out`. Returns the number of bytes written, or `None` when `out`
/// is shorter than `PAYLOAD_BYTES`.
///
/// Out-of-range field values are masked to their width rather than rejected,
/// which is the documented behaviour of all four implementations: a sequence
/// counter that wraps at 512 is meant to wrap, not to fail.
///
/// The signed field is converted with `as u32`, which Rust defines as the
/// wrapping two's complement conversion. That is the line the C version needs a
/// paragraph of comment to get right.
pub fn encode(out: &mut [u8], p: &Payload) -> Option<usize> {
    if out.len() < PAYLOAD_BYTES {
        return None;
    }
    out[..PAYLOAD_BYTES].fill(0);

    let mut w = Writer { buf: out, bit: 0 };
    w.put(p.version & M_VERSION, W_VERSION)?;
    w.put(p.flags & M_FLAGS, W_FLAGS)?;
    w.put(p.sequence & M_SEQUENCE, W_SEQUENCE)?;
    w.put(p.feature as u32 & M_FEATURE, W_FEATURE)?;
    w.put(p.battery & M_BATTERY, W_BATTERY)?;

    Some(PAYLOAD_BYTES)
}

fn get(input: &[u8], start: usize, width: u32) -> u32 {
    let mut v = 0u32;
    for i in 0..width as usize {
        let b = start + i;
        v = (v << 1) | u32::from((input[b / 8] >> (7 - (b % 8))) & 1);
    }
    v
}

/// Sign extension by arithmetic shift, which in Rust is what `>>` on a signed
/// integer means. Defined for every width from 2 to 32, and the specification
/// asserts the total is 40 bits so no field can exceed 32.
fn sign_extend(v: u32, width: u32) -> i32 {
    let shift = 32 - width;
    ((v << shift) as i32) >> shift
}

/// Decode exactly `PAYLOAD_BYTES` bytes.
pub fn decode(input: &[u8]) -> Result<Payload, DecodeError> {
    if input.len() != PAYLOAD_BYTES {
        return Err(DecodeError::WrongLength { got: input.len() });
    }
    Ok(Payload {
        version: get(input, O_VERSION, W_VERSION),
        flags: get(input, O_FLAGS, W_FLAGS),
        sequence: get(input, O_SEQUENCE, W_SEQUENCE),
        feature: sign_extend(get(input, O_FEATURE, W_FEATURE), W_FEATURE),
        battery: get(input, O_BATTERY, W_BATTERY),
    })
}

/// The masked form of a payload: what a round trip must return. Useful to a
/// caller comparing its own input against a decode, and used by the tests so
/// the expectation is computed from the field table rather than written twice.
pub fn masked(p: &Payload) -> Payload {
    Payload {
        version: p.version & M_VERSION,
        flags: p.flags & M_FLAGS,
        sequence: p.sequence & M_SEQUENCE,
        feature: sign_extend(p.feature as u32 & M_FEATURE, W_FEATURE),
        battery: p.battery & M_BATTERY,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The golden vectors are deliberately NOT duplicated here. They live in
    /// `../vectors.json`, hand computed, and no generator and no second copy
    /// has ever touched that file. The test that drives all four
    /// implementations against them is `python/tests/test_parity.py`, which
    /// runs this crate's filter binary. What belongs here is the properties
    /// that need no oracle.
    #[test]
    fn a_round_trip_returns_the_masked_input() {
        // A deterministic walk rather than a random one, so a failure names a
        // case that can be typed back in. The step is odd and coprime with
        // every field width's modulus, so every field varies independently.
        let mut n: u32 = 1;
        for _ in 0..20000 {
            n = n.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            let p = Payload {
                version: n & 0xFF,
                flags: (n >> 3) & 0xFF,
                sequence: (n >> 7) & 0x3FF,
                feature: (n as i32) >> 11,
                battery: (n >> 17) & 0x7F,
            };
            let mut buf = [0u8; PAYLOAD_BYTES];
            assert_eq!(encode(&mut buf, &p), Some(PAYLOAD_BYTES));
            assert_eq!(decode(&buf).unwrap(), masked(&p), "case {}", n);
        }
    }

    #[test]
    fn the_encoder_refuses_a_buffer_one_byte_short() {
        let mut buf = [0u8; PAYLOAD_BYTES - 1];
        assert_eq!(encode(&mut buf, &Payload::default()), None);
    }

    #[test]
    fn the_encoder_writes_nothing_past_its_five_bytes() {
        let mut buf = [0xAAu8; PAYLOAD_BYTES + 3];
        let p = Payload {
            version: M_VERSION,
            flags: M_FLAGS,
            sequence: M_SEQUENCE,
            feature: -1,
            battery: M_BATTERY,
        };
        assert_eq!(encode(&mut buf, &p), Some(PAYLOAD_BYTES));
        assert_eq!(&buf[PAYLOAD_BYTES..], &[0xAA, 0xAA, 0xAA]);
    }

    #[test]
    fn the_decoder_refuses_every_wrong_length() {
        for len in [0usize, 1, 4, 6, 10] {
            let buf = vec![0u8; len];
            assert_eq!(decode(&buf), Err(DecodeError::WrongLength { got: len }));
        }
    }

    /// The one property the C version needs a paragraph of comment to protect,
    /// asserted directly: the most negative value of the field survives, and it
    /// is not the same case as all bits set.
    #[test]
    fn sign_extension_reaches_both_ends_of_the_field() {
        let most_negative = -(1i32 << (W_FEATURE - 1));
        let most_positive = (1i32 << (W_FEATURE - 1)) - 1;
        for feature in [most_negative, -1, 0, 1, most_positive] {
            let p = Payload {
                feature,
                ..Payload::default()
            };
            let mut buf = [0u8; PAYLOAD_BYTES];
            encode(&mut buf, &p).unwrap();
            assert_eq!(decode(&buf).unwrap().feature, feature);
        }
        // One step past each end wraps into the field rather than being
        // rejected, which is the documented masking behaviour.
        let p = Payload {
            feature: most_positive + 1,
            ..Payload::default()
        };
        let mut buf = [0u8; PAYLOAD_BYTES];
        encode(&mut buf, &p).unwrap();
        assert_eq!(decode(&buf).unwrap().feature, most_negative);
    }

    #[test]
    fn the_field_table_sums_to_the_declared_width() {
        assert_eq!(
            W_VERSION + W_FLAGS + W_SEQUENCE + W_FEATURE + W_BATTERY,
            fields::PAYLOAD_BITS as u32
        );
        assert_eq!(PAYLOAD_BYTES * 8, fields::PAYLOAD_BITS);
    }
}
