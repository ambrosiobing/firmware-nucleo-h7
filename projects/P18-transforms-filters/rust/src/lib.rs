//! P18's cascade and acceptance metrics in Rust, against the same oracle as the
//! C, the C++ and the Python.
//!
//! The same arithmetic in the same order as `../../c/dspaccept.c`. The order
//! matters and is not a matter of style: a sum accumulated differently gives a
//! different double, and the parity test compares these numbers bit for bit on
//! every case the oracle marks exact.
//!
//! # Three languages, three answers to one duplication
//!
//! The cascade is the same code at two floating-point widths, and each language
//! here answers that differently, which is worth recording because it is the
//! same problem three times:
//!
//! * The C carries two functions, `biquad_df2t_f64` and `biquad_df2t_f32`, forty
//!   lines each and identical but for the type of five locals.
//! * The C++ carries one function template instantiated at `double` and at
//!   `float`, so the two cannot drift apart.
//! * Rust has no numeric tower in `core`: there is no trait in the standard
//!   library that `f32` and `f64` share for multiply and add. A generic version
//!   needs either the `num-traits` crate or a locally defined trait. This crate
//!   has no dependencies, by the rule the other three follow, so the two
//!   functions come from one `macro_rules!` body instead. One source of truth,
//!   like the C++, and reached by a different route.
//!
//! That is a cost of the language in a direction people do not usually expect,
//! and it is the second one this volume has found: P06's witness is `std` rather
//! than `no_std` because `f64::sqrt` lives in `std` and not in `core`.
//!
//! # This crate is std, for exactly that reason
//!
//! `accept_metrics` needs `sqrt` and `log10`. Both live in `std`, because
//! `core` assumes no platform and those functions live in the platform's libm.
//! A `no_std` version would need the `libm` crate. The metrics run on a host by
//! nature, so this is not a loss, and it is the same finding P06 records: the C
//! compiles for the target unchanged because newlib provides them, and the Rust
//! does not without a crate.

/// Eight stages is sixteenth order, far past anything this volume designs.
pub const MAX_STAGE: usize = 8;
pub const COEFF_PER_STAGE: usize = 5;

/// A post shift above seven means the coefficients are more than 128 times out
/// of range, which is a design that has not been scaled rather than a scale that
/// needs choosing.
pub const MAX_POST_SHIFT: i32 = 7;

/// The same six refusals as the C, in the same order, so a code means the same
/// thing in both. The filter prints the name and the parity test maps it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Argument,
    Stages,
    Shift,
    Range,
    Finite,
    Reference,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Stages => "stages",
            Error::Shift => "shift",
            Error::Range => "range",
            Error::Finite => "finite",
            Error::Reference => "reference",
        }
    }
}

/// Both metrics, and the flag that says the error energy was zero.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Accept {
    pub rel_rms: f64,
    pub snr_db: f64,
    pub exact: bool,
}

fn check_stages(nstage: usize, coeff_len: usize) -> Result<(), Error> {
    if nstage == 0 || nstage > MAX_STAGE {
        return Err(Error::Stages);
    }
    if coeff_len < nstage * COEFF_PER_STAGE {
        return Err(Error::Stages);
    }
    Ok(())
}

// One body, two widths. See the module comment for why this is a macro rather
// than a generic function.
macro_rules! cascade {
    ($name:ident, $t:ty) => {
        /// Transposed direct form 2, two state words per stage, state zeroed.
        pub fn $name(coeffs: &[$t], nstage: usize, data: &[$t]) -> Result<Vec<$t>, Error> {
            if data.is_empty() {
                return Err(Error::Argument);
            }
            check_stages(nstage, coeffs.len())?;
            let used = &coeffs[..nstage * COEFF_PER_STAGE];
            if used.iter().any(|c| !c.is_finite()) {
                return Err(Error::Finite);
            }

            let mut out = data.to_vec();
            for stage in used.chunks_exact(COEFF_PER_STAGE) {
                let (b0, b1, b2, a1, a2) = (stage[0], stage[1], stage[2], stage[3], stage[4]);
                let mut d1: $t = 0.0;
                let mut d2: $t = 0.0;
                for slot in out.iter_mut() {
                    let x = *slot;
                    let y = b0 * x + d1;
                    d1 = b1 * x + a1 * y + d2;
                    d2 = b2 * x + a2 * y;
                    *slot = y;
                }
            }
            Ok(out)
        }
    };
}

cascade!(biquad_df2t_f64, f64);
cascade!(biquad_df2t_f32, f32);

fn round_half_away_from_zero(v: f64) -> f64 {
    if v < 0.0 {
        -(-v + 0.5).floor()
    } else {
        (v + 0.5).floor()
    }
}

/// 32768 / 2^shift, built by halving so it is exact in binary at every shift.
fn q15_scale(post_shift: i32) -> f64 {
    let mut scale = 32768.0_f64;
    for _ in 0..post_shift {
        scale *= 0.5;
    }
    scale
}

fn fits(coeffs: &[f64], scale: f64) -> bool {
    coeffs.iter().all(|c| {
        let v = round_half_away_from_zero(c * scale);
        (-32768.0..=32767.0).contains(&v)
    })
}

/// The smallest post shift at which every coefficient fits in Q15.
pub fn q15_post_shift(coeffs: &[f64], nstage: usize) -> Result<i32, Error> {
    check_stages(nstage, coeffs.len())?;
    let used = &coeffs[..nstage * COEFF_PER_STAGE];
    if used.iter().any(|c| !c.is_finite()) {
        return Err(Error::Finite);
    }
    for shift in 0..=MAX_POST_SHIFT {
        if fits(used, q15_scale(shift)) {
            return Ok(shift);
        }
    }
    Err(Error::Range)
}

/// Convert at a stated shift, refusing anything that does not fit.
pub fn q15_coeffs(coeffs: &[f64], nstage: usize, post_shift: i32) -> Result<Vec<i16>, Error> {
    check_stages(nstage, coeffs.len())?;
    if !(0..=MAX_POST_SHIFT).contains(&post_shift) {
        return Err(Error::Shift);
    }
    let used = &coeffs[..nstage * COEFF_PER_STAGE];
    if used.iter().any(|c| !c.is_finite()) {
        return Err(Error::Finite);
    }
    let scale = q15_scale(post_shift);
    // Checked in full before anything is built, so a refusal returns nothing
    // rather than a half-converted vector.
    if !fits(used, scale) {
        return Err(Error::Range);
    }
    Ok(used
        .iter()
        .map(|c| round_half_away_from_zero(c * scale) as i16)
        .collect())
}

/// What an arithmetic right shift of `bits` places does, written as the floor
/// division it is.
///
/// Rust's `>>` on a signed value is already an arithmetic shift, where C's is
/// implementation defined, so this could have been `v >> bits`. It is
/// `div_euclid` for the same reason the C spells the division out: the rounding
/// direction of this one line decides every negative sample in the fixed-point
/// path, and it belongs in the source rather than in a language guarantee a
/// reader has to look up.
fn floor_shift(v: i64, bits: u32) -> i64 {
    v.div_euclid(1_i64 << bits)
}

/// Direct form 1 in Q15, returning the samples and the saturation count.
pub fn biquad_df1_q15(
    coeffs: &[i16],
    nstage: usize,
    post_shift: i32,
    data: &[i16],
) -> Result<(Vec<i16>, u32), Error> {
    if data.is_empty() {
        return Err(Error::Argument);
    }
    check_stages(nstage, coeffs.len())?;
    if !(0..=MAX_POST_SHIFT).contains(&post_shift) {
        return Err(Error::Shift);
    }

    let bits = (15 - post_shift) as u32;
    let mut saturations = 0_u32;
    let mut out = data.to_vec();

    for stage in coeffs[..nstage * COEFF_PER_STAGE].chunks_exact(COEFF_PER_STAGE) {
        let b0 = i64::from(stage[0]);
        let b1 = i64::from(stage[1]);
        let b2 = i64::from(stage[2]);
        let a1 = i64::from(stage[3]);
        let a2 = i64::from(stage[4]);
        let mut x1 = 0_i64;
        let mut x2 = 0_i64;
        let mut y1 = 0_i64;
        let mut y2 = 0_i64;
        for slot in out.iter_mut() {
            let x = i64::from(*slot);
            let acc = b0 * x + b1 * x1 + b2 * x2 + a1 * y1 + a2 * y2;
            let mut y = floor_shift(acc, bits);
            if y > 32767 {
                y = 32767;
                saturations += 1;
            } else if y < -32768 {
                y = -32768;
                saturations += 1;
            }
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            *slot = y as i16;
        }
    }
    Ok((out, saturations))
}

/// Both metrics in one pass.
///
/// Both lengths are implicit in the slices, and a mismatch is still refused
/// rather than trusted: comparing two blocks of different lengths against the
/// shorter of them returns a plausible number rather than an error, which is the
/// failure chapter 18 opens by naming. The C took one length until an oracle case
/// on Monday 26 October 2026 demanded a refusal that signature could not
/// express.
pub fn accept_metrics(got: &[f64], reference: &[f64]) -> Result<Accept, Error> {
    if got.is_empty() || got.len() != reference.len() {
        return Err(Error::Argument);
    }
    if got.iter().chain(reference.iter()).any(|v| !v.is_finite()) {
        return Err(Error::Finite);
    }

    // Summed in order, because the C adds in order and these numbers are
    // compared against it. A compensated sum would be a better number and a
    // worse twin.
    let mut mean_err2 = 0.0_f64;
    let mut mean_ref2 = 0.0_f64;
    for (g, r) in got.iter().zip(reference.iter()) {
        let e = g - r;
        mean_err2 += e * e;
        mean_ref2 += r * r;
    }
    let n = got.len() as f64;
    mean_err2 /= n;
    mean_ref2 /= n;

    if mean_ref2 == 0.0 {
        return Err(Error::Reference);
    }
    if mean_err2 == 0.0 {
        return Ok(Accept {
            rel_rms: 0.0,
            snr_db: f64::INFINITY,
            exact: true,
        });
    }
    Ok(Accept {
        rel_rms: mean_err2.sqrt() / mean_ref2.sqrt(),
        snr_db: 10.0 * (mean_ref2 / mean_err2).log10(),
        exact: false,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A gain of one half over Q15-exact inputs, which is exact in all three
    /// formats. The oracle's own version of this case is driven through the
    /// filter by the Python parity test; this one needs no oracle file and runs
    /// under `cargo test` alone.
    #[test]
    fn a_pure_gain_is_exact_in_both_widths() {
        let coeffs = [0.5_f64, 0.0, 0.0, 0.0, 0.0];
        let data = [0.5_f64, 0.25, -0.25, 0.0];
        let wide = biquad_df2t_f64(&coeffs, 1, &data).unwrap();
        assert_eq!(wide, vec![0.25, 0.125, -0.125, 0.0]);

        let coeffs32 = [0.5_f32, 0.0, 0.0, 0.0, 0.0];
        let data32 = [0.5_f32, 0.25, -0.25, 0.0];
        let narrow = biquad_df2t_f32(&coeffs32, 1, &data32).unwrap();
        assert_eq!(narrow, vec![0.25_f32, 0.125, -0.125, 0.0]);
    }

    /// The refusals, which are the part a reader checks first.
    #[test]
    fn the_refusals_are_the_cs_refusals() {
        // The arrays are named rather than written inline, and that is for the
        // formatter rather than for the reader: rustfmt keeps a call's
        // arguments on one line only while they fit fn_call_width, which is 60
        // by default and not the 100 of max_width. Inline literals pushed
        // eight of these ten past it, so it split them into five-line blocks
        // and `cargo fmt --all --check` refused the file. Named arrays leave
        // every line about fifty characters wide, which is comfortably inside
        // both limits and therefore has only one formatting.
        let unit = [0.5_f64, 0.0, 0.0, 0.0, 0.0];
        let two = [1.0_f64, 2.0, 1.0, 0.5, -0.25];
        let big = [200.0_f64, 0.0, 0.0, 0.0, 0.0];
        let one = [1.0_f64];
        let ones = [1.0_f64, 1.0];
        let zeros = [0.0_f64, 0.0];
        let nanny = [1.0_f64, f64::NAN];

        assert_eq!(biquad_df2t_f64(&unit, 1, &[]), Err(Error::Argument));
        assert_eq!(biquad_df2t_f64(&unit, 0, &one), Err(Error::Stages));
        assert_eq!(biquad_df2t_f64(&unit, 9, &one), Err(Error::Stages));
        assert_eq!(q15_coeffs(&unit, 1, -1), Err(Error::Shift));
        assert_eq!(q15_coeffs(&unit, 1, 8), Err(Error::Shift));
        assert_eq!(q15_coeffs(&two, 1, 1), Err(Error::Range));
        assert_eq!(q15_post_shift(&big, 1), Err(Error::Range));
        assert_eq!(accept_metrics(&ones, &zeros), Err(Error::Reference));
        assert_eq!(accept_metrics(&one, &ones), Err(Error::Argument));
        assert_eq!(accept_metrics(&nanny, &ones), Err(Error::Finite));
    }

    /// Two identical blocks have no error energy, which is the case chapter 18's
    /// own metric divides by.
    #[test]
    fn two_identical_blocks_are_exact_rather_than_infinite_decibels() {
        let a = [1.0_f64, -0.5, 0.25];
        let got = accept_metrics(&a, &a).unwrap();
        assert!(got.exact);
        assert_eq!(got.rel_rms, 0.0);
        assert!(got.snr_db.is_infinite());
    }

    /// The floor shift, directly, because it is the one line that decides every
    /// negative sample in the fixed-point path and the three languages reach it
    /// by three different routes: C by a division and a remainder test, C++ the
    /// same, and this by div_euclid.
    #[test]
    fn the_floor_shift_rounds_toward_negative_infinity() {
        assert_eq!(floor_shift(-1, 1), -1);
        assert_eq!(floor_shift(-2, 1), -1);
        assert_eq!(floor_shift(-3, 1), -2);
        assert_eq!(floor_shift(3, 1), 1);
    }
}
