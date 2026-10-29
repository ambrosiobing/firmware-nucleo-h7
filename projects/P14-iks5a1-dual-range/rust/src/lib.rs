//! P14's record header, its scale codes, the barometric relation and the drain
//! time. **std, and the reason is one function.**
//!
//! The same arithmetic, in the same order, as `../../c/recdecode.c` and
//! `../../python/recdecode.py`, against the same oracle. The three findings
//! this project is about are written out in
//! [`../../c/recdecode.h`](../../c/recdecode.h) and are not repeated here: the
//! event window selects the scale with the worst altitude resolution, the
//! altitude is differential and the reference is not in the record, and step
//! 10's figure omits the overhead its own comment names.
//!
//! WHY THIS CRATE IS std. Step 9's relation raises a pressure ratio to a
//! fractional power, and `f64::powf` is in std: core carries the operators and
//! the comparisons, and the transcendental functions belong to the standard
//! library. Writing a power series here instead would produce a number that is
//! not the chapter's relation and not what the twin computes, and agreement
//! with a twin is the whole point of the second implementation. So this crate
//! is std and the volume's list reads:
//!
//! ```text
//! P06, P18, P14   sqrt, log10 and powf are std, so those three crates are std
//! P12, P10        abs is std, so both write the magnitude out and stay no_std
//! P07, P17, P13   + - * / and comparison are core, so f64 alone needs no std
//! P11, and the integer crates, need nothing from either
//! ```
//!
//! THE SENSITIVITIES ARE THE CALLER'S TABLE AND NOT CONSTANTS HERE, which is
//! the C's decision and the chapter's reason one level further down: the scale
//! codes live in the record so that a record written under one configuration can
//! be read years later, and a sensitivity compiled into a decoder is exactly the
//! constant that removes. This bench holds neither sensor's datasheet, so a
//! table written here would be an invention; the oracle's table is powers of
//! two, which makes every conversion exact in binary and makes each finding
//! about the RATIO between two entries rather than about either value.
//!
//! A CODE WHOSE VALID BIT IS CLEAR IS REFUSED rather than read out of the slot.
//! The slot holds zero, which would convert every count to zero milli-g and
//! look like a quiet channel.
//!
//! THE HEADER IS PARSED BYTE BY BYTE AND NOT CAST, for the same reason the C
//! gives: the chapter declares a packed structure for the firmware to write,
//! and casting a byte buffer to it on a host assumes the host's byte order and
//! its tolerance for unaligned members. The whole value of a second
//! implementation is that it shares no assumption with the first. Here the
//! bytes are read with `u32::from_le_bytes` and `u16::from_le_bytes`, so the
//! little-endian contract is in the call rather than in the platform.

/// The header is fourteen bytes in the field order below.
pub const HDR_BYTES: usize = 14;
/// The acceleration full scales this format can express, as codes.
pub const FS_CODES: usize = 5;
pub const BARO_CODES: usize = 2;

pub const BARO_NARROW: u8 = 0;
pub const BARO_WIDE: u8 = 1;

/// The flags of step 7, bit for bit.
pub const FLAG_SHORT_WINDOW: u8 = 0x01;
pub const FLAG_EXTENDED: u8 = 0x02;
pub const FLAG_BARO_BLANKED: u8 = 0x04;
pub const FLAG_KNOWN: u8 = 0x07;

/// Step 9's exponent and scale factor, as the chapter prints them: the standard
/// atmosphere's, left exactly as written so a disagreement with the chapter is
/// visible rather than absorbed.
const ALT_SCALE: f64 = 44330.0;
const ALT_EXPONENT: f64 = 0.1903;

/// The same six refusals as the C, in the same order and with its names.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// A null pointer there; here, a buffer shorter than the header.
    Argument,
    FsCode,
    Baro,
    Flags,
    Counts,
    Pressure,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::FsCode => "fs_code",
            Error::Baro => "baro",
            Error::Flags => "flags",
            Error::Counts => "counts",
            Error::Pressure => "pressure",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Header {
    pub tick_event: u32,
    pub n_pre: u16,
    pub n_post: u16,
    pub n_baro: u16,
    pub lo_fs_g: u8,
    pub hi_fs_g: u8,
    pub baro_range: u8,
    pub flags: u8,
}

/// The code-to-sensitivity table, supplied by the caller for the reason the
/// crate comment gives. A code whose valid bit is clear is refused.
#[derive(Debug, Clone, Copy)]
pub struct Table {
    pub lo_mg_per_lsb: [f64; FS_CODES],
    pub hi_mg_per_lsb: [f64; FS_CODES],
    pub baro_lsb_per_hpa: [f64; BARO_CODES],
    pub lo_valid: u8,
    pub hi_valid: u8,
    pub baro_valid: u8,
}

#[derive(Debug, Clone, Copy)]
pub struct Scales {
    pub lo_mg_per_lsb: f64,
    pub hi_mg_per_lsb: f64,
    pub baro_lsb_per_hpa: f64,
}

/// The C's `!(x > 0.0)`, which refuses zero, every negative value and any value
/// that is not a number. clippy rejects the C's spelling as
/// `neg_cmp_op_on_partial_ord`.
fn not_above_zero(x: f64) -> bool {
    x.is_nan() || x <= 0.0
}

/// Parse the header from its bytes, little endian, in the field order of the
/// struct. Refuses rather than defaults on every code it does not recognise.
pub fn parse_header(buf: &[u8]) -> Result<Header, Error> {
    if buf.len() < HDR_BYTES {
        return Err(Error::Argument);
    }
    // from_le_bytes rather than a cast, so the byte order is in the call.
    let tick_event = u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]);
    let out = Header {
        tick_event,
        n_pre: u16::from_le_bytes([buf[4], buf[5]]),
        n_post: u16::from_le_bytes([buf[6], buf[7]]),
        n_baro: u16::from_le_bytes([buf[8], buf[9]]),
        lo_fs_g: buf[10],
        hi_fs_g: buf[11],
        baro_range: buf[12],
        flags: buf[13],
    };

    if usize::from(out.lo_fs_g) >= FS_CODES || usize::from(out.hi_fs_g) >= FS_CODES {
        return Err(Error::FsCode);
    }
    if usize::from(out.baro_range) >= BARO_CODES {
        return Err(Error::Baro);
    }
    // Parenthesised, although Rust binds & tighter than != and C does the
    // opposite: the reader of four implementations should not have to know
    // which language this file is in to read the expression.
    if (out.flags & !FLAG_KNOWN) != 0 {
        return Err(Error::Flags);
    }
    // A window cannot be both shorter than asked for and extended beyond it.
    // The chapter defines the two bits separately and never says they are
    // exclusive, which is the kind of thing a second implementation is for.
    if (out.flags & FLAG_SHORT_WINDOW) != 0 && (out.flags & FLAG_EXTENDED) != 0 {
        return Err(Error::Flags);
    }
    // A record claiming no samples at all on either acceleration channel is a
    // header without a record, and a pressure count of zero with the barometer
    // not blanked is the same thing for the other channel.
    if out.n_pre == 0 && out.n_post == 0 {
        return Err(Error::Counts);
    }
    if out.n_baro == 0 && (out.flags & FLAG_BARO_BLANKED) == 0 {
        return Err(Error::Counts);
    }
    Ok(out)
}

/// The scales the record asks for, looked up from its own codes.
pub fn scales(hdr: &Header, table: &Table) -> Result<Scales, Error> {
    let lo = usize::from(hdr.lo_fs_g);
    let hi = usize::from(hdr.hi_fs_g);
    let baro = usize::from(hdr.baro_range);
    if lo >= FS_CODES || hi >= FS_CODES {
        return Err(Error::FsCode);
    }
    if baro >= BARO_CODES {
        return Err(Error::Baro);
    }
    if ((table.lo_valid >> lo) & 1) == 0 || ((table.hi_valid >> hi) & 1) == 0 {
        return Err(Error::FsCode);
    }
    if ((table.baro_valid >> baro) & 1) == 0 {
        return Err(Error::Baro);
    }

    let out = Scales {
        lo_mg_per_lsb: table.lo_mg_per_lsb[lo],
        hi_mg_per_lsb: table.hi_mg_per_lsb[hi],
        baro_lsb_per_hpa: table.baro_lsb_per_hpa[baro],
    };
    if not_above_zero(out.lo_mg_per_lsb) || not_above_zero(out.hi_mg_per_lsb) {
        return Err(Error::FsCode);
    }
    if not_above_zero(out.baro_lsb_per_hpa) {
        return Err(Error::Baro);
    }
    Ok(out)
}

/// Counts to milli-g through the scale the record names. `fine` selects the fine
/// channel, because both are running the whole time and a record carries samples
/// from each.
pub fn counts_to_mg(hdr: &Header, table: &Table, counts: i16, fine: bool) -> Result<f64, Error> {
    let s = scales(hdr, table)?;
    let per_lsb = if fine {
        s.lo_mg_per_lsb
    } else {
        s.hi_mg_per_lsb
    };
    Ok(f64::from(counts) * per_lsb)
}

/// Counts to hectopascals through the range the record names.
pub fn counts_to_hpa(hdr: &Header, table: &Table, counts: i32) -> Result<f64, Error> {
    let s = scales(hdr, table)?;
    Ok(f64::from(counts) / s.baro_lsb_per_hpa)
}

/// Step 9's relation exactly, with the reference as an argument **because the
/// record does not carry one**, which is finding two. A default here would be
/// the compiled constant this project is about.
pub fn altitude_m(p_hpa: f64, p0_hpa: f64) -> Result<f64, Error> {
    if not_above_zero(p_hpa) || not_above_zero(p0_hpa) {
        return Err(Error::Pressure);
    }
    Ok(ALT_SCALE * (1.0 - (p_hpa / p0_hpa).powf(ALT_EXPONENT)))
}

/// Metres per least significant bit of pressure at a stated pressure, which is
/// what finding one is about.
pub fn altitude_resolution_m(lsb_per_hpa: f64, p_hpa: f64, p0_hpa: f64) -> Result<f64, Error> {
    if not_above_zero(lsb_per_hpa) || not_above_zero(p_hpa) || not_above_zero(p0_hpa) {
        return Err(Error::Pressure);
    }
    // The derivative of step 9's relation, times one least significant bit.
    // Taken analytically rather than by differencing two altitudes, because a
    // difference of two nearly equal altitudes is where the resolution of this
    // very calculation would show up as noise.
    let slope = ALT_SCALE * ALT_EXPONENT / p0_hpa * (p_hpa / p0_hpa).powf(ALT_EXPONENT - 1.0);
    Ok(slope / lsb_per_hpa)
}

/// Step 10's drain time. `overhead_bytes` is the address and register cost the
/// chapter's comment names and its sum omits: pass 0 to reproduce the printed
/// figure and 2 for the transaction the comment describes.
pub fn drain_ms(
    samples: u32,
    bytes_per_sample: u32,
    overhead_bytes: u32,
    bus_hz: f64,
) -> Result<f64, Error> {
    if samples == 0 || bytes_per_sample == 0 {
        return Err(Error::Counts);
    }
    if not_above_zero(bus_hz) {
        return Err(Error::Pressure);
    }
    // Nine bit times a byte, which is the eight bits and the acknowledgement,
    // exactly as the chapter's comment states it.
    let bytes = f64::from(samples) * f64::from(bytes_per_sample) + f64::from(overhead_bytes);
    Ok(1000.0 * (bytes * 9.0) / bus_hz)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The oracle's table: powers of two, so every conversion is exact in
    /// binary and each finding is about a ratio rather than a value.
    fn table() -> Table {
        Table {
            lo_mg_per_lsb: [0.0625, 0.125, 0.25, 0.5, 0.0],
            hi_mg_per_lsb: [1.0, 2.0, 4.0, 8.0, 0.0],
            baro_lsb_per_hpa: [4096.0, 2048.0],
            lo_valid: 15,
            hi_valid: 15,
            baro_valid: 3,
        }
    }

    fn bytes() -> [u8; 14] {
        // tick 1000, 64 pre, 64 post, 8 baro, codes 0, no flags.
        [232, 3, 0, 0, 64, 0, 64, 0, 8, 0, 0, 0, 0, 0]
    }

    #[test]
    fn the_header_is_read_little_endian_in_the_field_order() {
        let hdr = parse_header(&bytes()).expect("a plain record");
        assert_eq!(hdr.tick_event, 1000);
        assert_eq!((hdr.n_pre, hdr.n_post, hdr.n_baro), (64, 64, 8));
        assert_eq!(hdr.flags, 0);
    }

    #[test]
    fn a_window_cannot_be_both_shortened_and_extended() {
        let mut b = bytes();
        b[13] = FLAG_SHORT_WINDOW | FLAG_EXTENDED;
        assert_eq!(parse_header(&b), Err(Error::Flags));
        b[13] = FLAG_SHORT_WINDOW;
        assert!(parse_header(&b).is_ok());
    }

    #[test]
    fn a_pressure_count_of_zero_needs_the_blanked_flag() {
        let mut b = bytes();
        b[8] = 0;
        assert_eq!(parse_header(&b), Err(Error::Counts));
        b[13] = FLAG_BARO_BLANKED;
        assert!(parse_header(&b).is_ok());
    }

    #[test]
    fn a_code_the_table_does_not_define_is_refused_and_not_read_as_zero() {
        let mut b = bytes();
        b[10] = 4; // the format can express it; the table's valid bit is clear
        let hdr = parse_header(&b).expect("the format accepts code four");
        assert_eq!(counts_to_mg(&hdr, &table(), 1000, true), Err(Error::FsCode));
    }

    #[test]
    fn the_conversions_are_exact_because_the_table_is_powers_of_two() {
        let hdr = parse_header(&bytes()).expect("a plain record");
        let t = table();
        assert_eq!(counts_to_mg(&hdr, &t, 1000, true), Ok(62.5));
        assert_eq!(counts_to_mg(&hdr, &t, 1000, false), Ok(1000.0));
        assert_eq!(counts_to_hpa(&hdr, &t, 4149248), Ok(1013.0));
    }

    #[test]
    fn the_altitude_is_zero_at_the_reference_and_refuses_a_zero_pressure() {
        assert_eq!(altitude_m(1013.25, 1013.25), Ok(0.0));
        assert_eq!(altitude_m(0.0, 1013.25), Err(Error::Pressure));
        assert_eq!(altitude_m(1013.25, 0.0), Err(Error::Pressure));
    }

    #[test]
    fn the_wide_range_halves_the_altitude_resolution_which_is_finding_one() {
        let t = table();
        let narrow = t.baro_lsb_per_hpa[usize::from(BARO_NARROW)];
        let wide = t.baro_lsb_per_hpa[usize::from(BARO_WIDE)];
        let a = altitude_resolution_m(narrow, 1013.25, 1013.25).unwrap();
        let b = altitude_resolution_m(wide, 1013.25, 1013.25).unwrap();
        // The ratio of the two sensitivities, exactly, because the table is
        // powers of two and the pressure is the same in both calls.
        assert_eq!(b / a, narrow / wide);
        assert!(b > a, "the wide range resolves altitude less finely");
    }

    #[test]
    fn step_tens_figure_and_the_transaction_its_comment_describes() {
        // 128 samples of 6 bytes at 400 kHz, nine bit times a byte.
        assert_eq!(drain_ms(128, 6, 0, 400000.0), Ok(17.28));
        let with_overhead = drain_ms(128, 6, 2, 400000.0).unwrap();
        assert!(with_overhead > 17.28);
        // The difference is 0.045 ms, which changes no conclusion and is
        // recorded because the comment and the sum disagree.
        assert!((with_overhead - 17.28 - 0.045).abs() < 1e-12);
    }

    #[test]
    fn the_refusals_are_the_cs_refusals() {
        assert_eq!(parse_header(&[0u8; 13]), Err(Error::Argument));
        assert_eq!(drain_ms(0, 6, 0, 400000.0), Err(Error::Counts));
        assert_eq!(drain_ms(128, 0, 0, 400000.0), Err(Error::Counts));
        assert_eq!(drain_ms(128, 6, 0, 0.0), Err(Error::Pressure));
        assert_eq!(
            altitude_resolution_m(0.0, 1013.25, 1013.25),
            Err(Error::Pressure)
        );
    }
}
