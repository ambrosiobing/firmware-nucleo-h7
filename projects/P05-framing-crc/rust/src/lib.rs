//! P05's framing in Rust: COBS, CRC-16 and the frame, `no_std`.
//!
//! The same format as the C in `../c/`, the C++ in `../cpp/` and the Python in
//! `../python/twin.py`, written independently so the four-way comparison means
//! something.
//!
//! On the wire:  `[ COBS( payload || crc16(payload) ) ] [ 0x00 ]`
//!
//! What Rust adds over the C, and it is the same one thing the C++ adds: the
//! checksum is a `const fn`, and the published check value 0x29B1 over
//! `"123456789"` is asserted at compile time at the bottom of this file. A crate
//! whose parameters are not the published ones does not compile.
//!
//! What this deliberately does differently from the C, in one place. The C's
//! `cobs_decode` returns 0 both for a corrupt frame and for a frame that decodes
//! to zero bytes, which the single byte `0x01` does, legitimately. So the C's
//! `frame_decode` reports that frame as a stuffing error when it is a frame that
//! is too short to carry its checksum. Here `cobs_decode` returns `Option<usize>`
//! and `Some(0)` is not a failure, which is what the Python twin does with
//! `None`. The parity test pins that single divergence by name.

#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]

/// The six parameters, and all six matter. The reversal settings are the ones
/// that go wrong in practice: a checksum with the input bits reflected is a
/// perfectly good checksum that agrees with nothing published.
pub const CRC16_POLY: u16 = 0x1021;
pub const CRC16_INIT: u16 = 0xFFFF;
/// Over the nine bytes `"123456789"`. Every catalogue of these polynomials
/// lists it, which makes it the only check here that is not self-referential.
pub const CRC16_CHECK_VALUE: u16 = 0x29B1;

pub const DELIMITER: u8 = 0x00;
pub const CRC_BYTES: usize = 2;
pub const MAX_PAYLOAD: usize = 64;

/// Bitwise, as the C reference is, and `const`. A table would be faster and
/// would move the check value from the compiler to a test.
pub const fn crc16(data: &[u8]) -> u16 {
    let mut crc = CRC16_INIT;
    let mut i = 0;
    while i < data.len() {
        // No input reflection: the byte enters the high half as it is.
        crc ^= (data[i] as u16) << 8;
        let mut bit = 0;
        while bit < 8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ CRC16_POLY
            } else {
                crc << 1
            };
            bit += 1;
        }
        i += 1;
    }
    crc // no final exclusive-or, no output reflection
}

// ------------------------------------------------------------------- COBS

/// The largest encoding of `len` bytes: one code byte per run of up to 254,
/// plus one for the first run.
pub const fn cobs_encode_max(len: usize) -> usize {
    len + len / 254 + 1
}

/// The encoded length, or `None` when `dst` is too small. The delimiter is not
/// appended: it belongs to the stream, not to the encoding.
pub fn cobs_encode(src: &[u8], dst: &mut [u8]) -> Option<usize> {
    if dst.len() < cobs_encode_max(src.len()) {
        return None;
    }
    let mut code_at = 0; // where the code byte for the run in progress goes
    let mut out = 1; // the slot after it
    let mut code: u8 = 1; // counts itself

    for &byte in src {
        if byte != 0 {
            dst[out] = byte;
            out += 1;
            code += 1;
        }
        // A zero ends the run and is represented by the code rather than
        // written. A run of 254 non-zero bytes ends too: the code cannot count
        // higher.
        if byte == 0 || code == 0xFF {
            dst[code_at] = code;
            code_at = out;
            out += 1;
            code = 1;
        }
    }
    dst[code_at] = code;
    Some(out)
}

/// The decoded length, or `None` on a corrupt frame: a zero inside the data, a
/// code that promised more bytes than arrived, or an overrun of `dst`.
/// `Some(0)` is a frame that decoded to nothing, which is not corruption.
pub fn cobs_decode(src: &[u8], dst: &mut [u8]) -> Option<usize> {
    let mut i = 0;
    let mut out = 0;

    while i < src.len() {
        let code = src[i];
        i += 1;
        if code == 0 {
            return None; // the delimiter cannot appear inside a frame
        }
        for _ in 1..code {
            if i >= src.len() || out >= dst.len() {
                return None; // the code promised more than arrived
            }
            dst[out] = src[i];
            out += 1;
            i += 1;
        }
        // A run shorter than 254 ended on a zero in the original, so put it
        // back. A run of exactly 254 did not, and a run at the end did not.
        if code != 0xFF && i < src.len() {
            if out >= dst.len() {
                return None;
            }
            dst[out] = 0;
            out += 1;
        }
    }
    Some(out)
}

// ------------------------------------------------------------------ frame

/// A decode that did not fail: either an idle line, or a payload of this many
/// bytes written into the caller's buffer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Decoded {
    /// Nothing between two delimiters. What an idle line looks like, discarded
    /// rather than reported.
    Empty,
    /// A payload of this length, checksum verified.
    Payload(usize),
}

/// Every way a decode can refuse. Separate variants rather than one failure,
/// because the counts are different findings: stuffing errors mean the
/// receiver lost its place, checksum errors mean the line corrupted a byte.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DecodeError {
    /// Truncated, or a zero inside the data.
    Stuffing,
    /// Shorter than its own checksum.
    TooShort,
    /// Payload longer than the caller's buffer.
    TooLong,
    /// The line corrupted at least one byte.
    Checksum,
}

/// The name each verdict has in the parity protocol, shared with the other
/// three implementations. The C has these as `frame_result_t`.
pub fn verdict_name(r: Result<Decoded, DecodeError>) -> &'static str {
    match r {
        Ok(Decoded::Empty) => "empty",
        Ok(Decoded::Payload(_)) => "ok",
        Err(DecodeError::Stuffing) => "stuffing",
        Err(DecodeError::TooShort) => "too_short",
        Err(DecodeError::TooLong) => "too_long",
        Err(DecodeError::Checksum) => "checksum",
    }
}

/// The largest frame a payload of this length can produce, delimiter included.
pub const fn frame_encode_max(payload_len: usize) -> usize {
    cobs_encode_max(payload_len + CRC_BYTES) + 1
}

/// The frame length including the delimiter, or `None` when the payload is
/// longer than `MAX_PAYLOAD` or `out` is too small.
pub fn frame_encode(payload: &[u8], out: &mut [u8]) -> Option<usize> {
    let len = payload.len();
    if len > MAX_PAYLOAD || out.len() < frame_encode_max(len) {
        return None;
    }
    // Payload then checksum as one buffer, so the stuffing covers both.
    let mut body = [0u8; MAX_PAYLOAD + CRC_BYTES];
    body[..len].copy_from_slice(payload);
    let crc = crc16(payload);
    body[len] = (crc >> 8) as u8; // most significant first
    body[len + 1] = (crc & 0xFF) as u8;

    let cap = out.len() - 1; // the last slot is the delimiter's
    let stuffed = cobs_encode(&body[..len + CRC_BYTES], &mut out[..cap])?;
    out[stuffed] = DELIMITER;
    Some(stuffed + 1)
}

/// Decodes what lay between two delimiters. On `Payload(n)` the first `n` bytes
/// of `payload` are written; on anything else nothing is written, because a
/// frame that fails is never partially delivered.
pub fn frame_decode(stuffed: &[u8], payload: &mut [u8]) -> Result<Decoded, DecodeError> {
    if stuffed.is_empty() {
        return Ok(Decoded::Empty);
    }
    let mut body = [0u8; MAX_PAYLOAD + CRC_BYTES];
    let decoded = cobs_decode(stuffed, &mut body).ok_or(DecodeError::Stuffing)?;
    if decoded < CRC_BYTES + 1 {
        return Err(DecodeError::TooShort);
    }
    let n = decoded - CRC_BYTES;
    let got = (u16::from(body[n]) << 8) | u16::from(body[n + 1]);
    let want = crc16(&body[..n]);
    if got != want {
        return Err(DecodeError::Checksum);
    }
    if n > payload.len() {
        return Err(DecodeError::TooLong);
    }
    payload[..n].copy_from_slice(&body[..n]);
    Ok(Decoded::Payload(n))
}

// The check value, asserted by the compiler. If the six parameters above are
// not the published ones, this crate does not build.
const _: () = assert!(
    crc16(b"123456789") == CRC16_CHECK_VALUE,
    "the CRC-16 parameters do not reproduce the published check value 0x29B1"
);

#[cfg(test)]
mod tests {
    use super::*;

    /// The random payloads and the corruption set are deliberately NOT here.
    /// They live in `python/tests/test_frame_parity.py`, which drives all four
    /// implementations with one case list. What belongs here is the properties
    /// that need no other implementation to be meaningful.
    #[test]
    fn the_check_value_holds_at_run_time_as_well() {
        assert_eq!(crc16(b"123456789"), 0x29B1);
    }

    #[test]
    fn a_round_trip_returns_the_payload() {
        let mut n: u32 = 7;
        for _ in 0..3000 {
            n = n.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            let len = (n % 64) as usize + 1;
            let mut payload = [0u8; MAX_PAYLOAD];
            for (k, slot) in payload[..len].iter_mut().enumerate() {
                *slot = ((n >> (k % 24)) & 0xFF) as u8;
            }
            let mut frame = [0u8; 128];
            let m = frame_encode(&payload[..len], &mut frame).expect("encodes");
            assert_eq!(frame[m - 1], DELIMITER);
            assert!(
                !frame[..m - 1].contains(&0),
                "a zero escaped into the frame"
            );
            let mut back = [0u8; MAX_PAYLOAD];
            assert_eq!(
                frame_decode(&frame[..m - 1], &mut back),
                Ok(Decoded::Payload(len))
            );
            assert_eq!(&back[..len], &payload[..len]);
        }
    }

    #[test]
    fn payloads_full_of_zeros_survive_the_stuffing() {
        for payload in [&[0u8][..], &[0u8; 10], &[0, 1, 0, 2, 0], &[0u8; 64]] {
            let mut frame = [0u8; 128];
            let m = frame_encode(payload, &mut frame).unwrap();
            assert!(!frame[..m - 1].contains(&0));
            let mut back = [0u8; MAX_PAYLOAD];
            assert_eq!(
                frame_decode(&frame[..m - 1], &mut back),
                Ok(Decoded::Payload(payload.len()))
            );
            assert_eq!(&back[..payload.len()], payload);
        }
    }

    #[test]
    fn the_five_byte_payload_frames_to_nine_bytes() {
        let mut frame = [0u8; 16];
        let m = frame_encode(&[0x24, 0x05, 0xFF, 0xFF, 0xE8], &mut frame).unwrap();
        assert_eq!(m, 9);
    }

    #[test]
    fn a_payload_longer_than_the_format_allows_is_refused_at_encode() {
        let mut frame = [0u8; 256];
        assert_eq!(frame_encode(&[1u8; MAX_PAYLOAD + 1], &mut frame), None);
    }

    #[test]
    fn every_single_bit_flip_is_rejected() {
        let payload = [0x24u8, 0x05, 0xFF, 0xFF, 0xE8, 0x00, 0x7F];
        let mut frame = [0u8; 32];
        let m = frame_encode(&payload, &mut frame).unwrap();
        let stuffed = &frame[..m - 1];
        for i in 0..stuffed.len() {
            for bit in 0..8 {
                let mut bad = [0u8; 32];
                bad[..stuffed.len()].copy_from_slice(stuffed);
                bad[i] ^= 1 << bit;
                let mut back = [0u8; MAX_PAYLOAD];
                let r = frame_decode(&bad[..stuffed.len()], &mut back);
                let delivered_intact =
                    r == Ok(Decoded::Payload(payload.len())) && back[..payload.len()] == payload;
                assert!(!delivered_intact, "byte {} bit {} got through", i, bit);
            }
        }
    }

    #[test]
    fn an_empty_run_is_not_an_error_and_a_lone_code_byte_is_too_short() {
        let mut back = [0u8; MAX_PAYLOAD];
        assert_eq!(frame_decode(&[], &mut back), Ok(Decoded::Empty));
        // The single byte 0x01 decodes to zero bytes. That is a frame too short
        // to carry its checksum, and this names it so, where the C names it a
        // stuffing error because its cobs_decode cannot tell empty from failed.
        assert_eq!(frame_decode(&[0x01], &mut back), Err(DecodeError::TooShort));
    }
}
