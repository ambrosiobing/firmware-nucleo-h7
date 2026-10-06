//! P01's clock tree decode in Rust, held to the same oracle as the C.
//!
//! `no_std`, no allocation, no dependencies: masks, shifts, integer division and
//! one rounding rule.
//!
//! # Where Rust has to be told what to do
//!
//! This is the language that makes the fewest of these arithmetic decisions for
//! you, which is why it is the interesting one to write third.
//!
//! - **Overflow panics in debug and wraps in release.** The C's guard against
//!   `ref/m * n` exceeding 32 bits is written as a comparison, not left to the
//!   type, and the same comparison is here. Without it this crate would panic
//!   under `cargo test` and silently wrap under `--release`, which is two
//!   different wrong answers from one source file.
//! - **Division truncates toward zero**, as C's does, so the frequencies need no
//!   special handling. The parts per million do: halves must go away from zero,
//!   which `/` does not give, and `i64::midpoint` or a cast through `f64` would
//!   each be a different rule.
//! - **Saturation is explicit.** `as i32` would wrap, and `i32::try_from` would
//!   give an error this protocol has no answer for, so the clamp is written out.
//!
//! # Why the field positions are duplicated here
//!
//! They are written out below rather than shared with the C, and that is the
//! point of a second implementation rather than an oversight. An implementation
//! that read the C's header could not disagree with it about where a field sits,
//! so it could not catch the error it exists to catch: that `DIVM1` holds the
//! value while `N1` and `P1` hold the value minus one. The authority for every
//! number is the same as the C's and is named at each define in
//! `c/board/stm32h7a3_regs.h`: ST's CMSIS device header for this die.

#![no_std]

/// The seven words the decode reads, in the order `clocktree_regs_t` declares.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Regs {
    pub cr: u32,
    pub cfgr: u32,
    pub pllckselr: u32,
    pub pllcfgr: u32,
    pub pll1divr: u32,
    pub cdcfgr1: u32,
    pub cdcfgr2: u32,
}

/// The eight ways the decode gives up, in the order they are tested.
///
/// The discriminants match the C's enum. The parity test compares the TOKENS, so
/// a mismatch would still be caught, but matching them means a reader comparing
/// the four files is not asked to hold four orderings in mind.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Refusal {
    Ok = 0,
    SwsUnknown = 1,
    HseNotBypass = 2,
    PllFractional = 3,
    PllPDisabled = 4,
    PllSourceUnknown = 5,
    PllZeroDivider = 6,
    PllWouldOverflow = 7,
    PrescalerUndecoded = 8,
}

impl Refusal {
    /// The nine tokens, identical to `clocktree_refusal_text` in the C.
    #[must_use]
    pub const fn text(self) -> &'static str {
        match self {
            Refusal::Ok => "ok",
            Refusal::SwsUnknown => "sws-unknown",
            Refusal::HseNotBypass => "hse-not-bypass",
            Refusal::PllFractional => "pll-fractional",
            Refusal::PllPDisabled => "pll-p-disabled",
            Refusal::PllSourceUnknown => "pll-source-unknown",
            Refusal::PllZeroDivider => "pll-zero-divider",
            Refusal::PllWouldOverflow => "pll-would-overflow",
            Refusal::PrescalerUndecoded => "prescaler-undecoded",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Tree {
    pub sys_hz: u32,
    pub core_hz: u32,
    pub ahb_hz: u32,
    pub pclk1_hz: u32,
    pub pclk2_hz: u32,
    pub refusal: Refusal,
}

impl Tree {
    const fn refused(why: Refusal) -> Self {
        Self {
            sys_hz: 0,
            core_hz: 0,
            ahb_hz: 0,
            pclk1_hz: 0,
            pclk2_hz: 0,
            refusal: why,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Bias {
    pub ok: bool,
    pub frequency_ppm: i32,
    pub duration_ppm: i32,
    pub delay_ppm: i32,
}

mod field {
    pub const CR_HSIDIV_MSK: u32 = 3 << 3;
    pub const CR_HSIDIV_POS: u32 = 3;
    pub const CR_HSEBYP_MSK: u32 = 1 << 18;
    pub const CR_HSERDY_MSK: u32 = 1 << 17;

    pub const CFGR_SWS_MSK: u32 = 7 << 3;
    pub const CFGR_SWS_POS: u32 = 3;
    pub const SWS_HSI: u32 = 0;
    pub const SWS_HSE: u32 = 2;
    pub const SWS_PLL1: u32 = 3;

    pub const PLLCKSELR_PLLSRC_MSK: u32 = 3;
    pub const PLLSRC_HSE: u32 = 2;
    pub const PLLCKSELR_DIVM1_MSK: u32 = 0x3F << 4;
    pub const PLLCKSELR_DIVM1_POS: u32 = 4;

    pub const PLLCFGR_PLL1FRACEN_MSK: u32 = 1;
    pub const PLLCFGR_DIVP1EN_MSK: u32 = 1 << 16;

    pub const PLL1DIVR_N1_MSK: u32 = 0x1FF;
    pub const PLL1DIVR_N1_POS: u32 = 0;
    pub const PLL1DIVR_P1_MSK: u32 = 0x7F << 9;
    pub const PLL1DIVR_P1_POS: u32 = 9;

    pub const CDCFGR1_HPRE_MSK: u32 = 0xF;
    pub const CDCFGR1_HPRE_POS: u32 = 0;
    pub const CDCFGR1_CDCPRE_MSK: u32 = 0xF << 8;
    pub const CDCFGR1_CDCPRE_POS: u32 = 8;
    pub const CDCFGR2_CDPPRE1_MSK: u32 = 7 << 4;
    pub const CDCFGR2_CDPPRE1_POS: u32 = 4;
    // CDPPRE2, bits 10:8 of the same word, written out rather than derived.
    pub const CDCFGR2_CDPPRE2_MSK: u32 = 7 << 8;
    pub const CDCFGR2_CDPPRE2_POS: u32 = 8;

    pub const AHBPRE_DIV1: u32 = 0x0;
    pub const AHBPRE_DIV2: u32 = 0x8;
    pub const AHBPRE_DIV4: u32 = 0x9;
    pub const APBPRE_DIV1: u32 = 0x0;
    pub const APBPRE_DIV2: u32 = 0x4;
    pub const APBPRE_DIV4: u32 = 0x5;
}

/// Three of sixteen encodings are sourced and the rest deliberately are not,
/// because the remaining ratios are not evenly spaced. 0 means refuse.
const fn ahb_cpu_divider(f: u32) -> u32 {
    match f {
        field::AHBPRE_DIV1 => 1,
        field::AHBPRE_DIV2 => 2,
        field::AHBPRE_DIV4 => 4,
        _ => 0,
    }
}

const fn apb_divider(f: u32) -> u32 {
    match f {
        field::APBPRE_DIV1 => 1,
        field::APBPRE_DIV2 => 2,
        field::APBPRE_DIV4 => 4,
        _ => 0,
    }
}

/// HSIDIV is two bits and the divisor is `1 << field`, so this is an exact shift
/// and not a rounded division.
const fn hsi_hz(r: &Regs, nominal: u32) -> u32 {
    nominal >> ((r.cr & field::CR_HSIDIV_MSK) >> field::CR_HSIDIV_POS)
}

/// A board fact and not a register fact: with the bypass bit clear the part is
/// running from an oscillator this repository knows nothing about, and returning
/// 8 MHz then would assert a board fact that does not apply.
const fn hse_hz(r: &Regs, bypass: u32) -> u32 {
    if (r.cr & field::CR_HSEBYP_MSK) == 0 || (r.cr & field::CR_HSERDY_MSK) == 0 {
        return 0;
    }
    bypass
}

/// PLL1's P output, or a reason.
///
/// The reference is divided by `M` first and multiplied by `N` second, so an `M`
/// that does not divide exactly loses the remainder before the multiply. The
/// oracle carries the `M` of 3 case because the opposite order is just as
/// plausible to write and gives 373333333 where this gives 373333240.
const fn pll1_p_hz(r: &Regs, hsi_nominal: u32, hse_bypass: u32) -> (u32, Refusal) {
    if (r.pllcfgr & field::PLLCFGR_PLL1FRACEN_MSK) != 0 {
        return (0, Refusal::PllFractional);
    }
    if (r.pllcfgr & field::PLLCFGR_DIVP1EN_MSK) == 0 {
        return (0, Refusal::PllPDisabled);
    }

    let reference = match r.pllckselr & field::PLLCKSELR_PLLSRC_MSK {
        0 => hsi_hz(r, hsi_nominal),
        field::PLLSRC_HSE => {
            let hse = hse_hz(r, hse_bypass);
            if hse == 0 {
                return (0, Refusal::HseNotBypass);
            }
            hse
        }
        _ => return (0, Refusal::PllSourceUnknown),
    };
    if reference == 0 {
        return (0, Refusal::PllSourceUnknown);
    }

    // DIVM1 holds the value; N1 and P1 hold the value minus one. ST's asymmetry,
    // and the single most likely thing for a second implementation to get wrong.
    let m = (r.pllckselr & field::PLLCKSELR_DIVM1_MSK) >> field::PLLCKSELR_DIVM1_POS;
    let n = ((r.pll1divr & field::PLL1DIVR_N1_MSK) >> field::PLL1DIVR_N1_POS) + 1;
    let p = ((r.pll1divr & field::PLL1DIVR_P1_MSK) >> field::PLL1DIVR_P1_POS) + 1;

    if m == 0 {
        return (0, Refusal::PllZeroDivider);
    }
    let input = reference / m;
    // The comparison the C needs because of its types, and that this crate needs
    // because without it the multiply panics under test and wraps under release.
    if input == 0 || input > (u32::MAX / n) {
        return (0, Refusal::PllWouldOverflow);
    }
    ((input * n) / p, Refusal::Ok)
}

/// Decode the tree, or say which guard refused it.
#[must_use]
pub const fn decode(r: &Regs, hsi_nominal: u32, hse_bypass: u32) -> Tree {
    let sys = match (r.cfgr & field::CFGR_SWS_MSK) >> field::CFGR_SWS_POS {
        field::SWS_HSI => hsi_hz(r, hsi_nominal),
        field::SWS_HSE => {
            let hse = hse_hz(r, hse_bypass);
            if hse == 0 {
                return Tree::refused(Refusal::HseNotBypass);
            }
            hse
        }
        field::SWS_PLL1 => {
            let (hz, why) = pll1_p_hz(r, hsi_nominal, hse_bypass);
            if hz == 0 {
                return Tree::refused(why);
            }
            hz
        }
        _ => return Tree::refused(Refusal::SwsUnknown),
    };
    if sys == 0 {
        return Tree::refused(Refusal::SwsUnknown);
    }

    let cpu_div =
        ahb_cpu_divider((r.cdcfgr1 & field::CDCFGR1_CDCPRE_MSK) >> field::CDCFGR1_CDCPRE_POS);
    let ahb_div = ahb_cpu_divider((r.cdcfgr1 & field::CDCFGR1_HPRE_MSK) >> field::CDCFGR1_HPRE_POS);
    let apb1_div =
        apb_divider((r.cdcfgr2 & field::CDCFGR2_CDPPRE1_MSK) >> field::CDCFGR2_CDPPRE1_POS);
    let apb2_div =
        apb_divider((r.cdcfgr2 & field::CDCFGR2_CDPPRE2_MSK) >> field::CDCFGR2_CDPPRE2_POS);

    if cpu_div == 0 || ahb_div == 0 || apb1_div == 0 || apb2_div == 0 {
        // Reporting the undivided frequency would be wrong by that very ratio,
        // which is the one error nobody would suspect.
        return Tree::refused(Refusal::PrescalerUndecoded);
    }

    // sys_ck over CDCPRE is the core, the core over HPRE is the AHB buses, the
    // AHB over CDPPRE1 is APB1.
    let core_hz = sys / cpu_div;
    let ahb_hz = core_hz / ahb_div;
    Tree {
        sys_hz: sys,
        core_hz,
        ahb_hz,
        pclk1_hz: ahb_hz / apb1_div,
        // Off the AHB, not off APB1, even though they are equal on this board.
        pclk2_hz: ahb_hz / apb2_div,
        refusal: Refusal::Ok,
    }
}

/// Parts per million, halves away from zero, saturating at the `i32` bounds.
///
/// `/` truncates toward zero, which is what the C does, so the sign handling is
/// in the bias added before the division and not in a second branch afterwards.
/// The clamp is written out because `as i32` would wrap.
const fn ppm(delta: i64, den: i64) -> i32 {
    let half = den / 2;
    let scaled = delta * 1_000_000;
    let q = if delta >= 0 {
        (scaled + half) / den
    } else {
        (scaled - half) / den
    };
    if q > i32::MAX as i64 {
        return i32::MAX;
    }
    if q < i32::MIN as i64 {
        return i32::MIN;
    }
    q as i32
}

/// The signed bias: three figures that are two quantities.
///
/// `delay_ppm` equals `frequency_ppm` by the same expression rather than by
/// copying a result: both divide by the true frequency. `duration_ppm` divides
/// by the reported one, so it differs in the last unit or two.
#[must_use]
pub const fn bias(reported_hz: u32, true_hz: u32) -> Bias {
    if reported_hz == 0 || true_hz == 0 {
        return Bias {
            ok: false,
            frequency_ppm: 0,
            duration_ppm: 0,
            delay_ppm: 0,
        };
    }
    let reported = reported_hz as i64;
    let truth = true_hz as i64;
    let frequency_ppm = ppm(reported - truth, truth);
    Bias {
        ok: true,
        frequency_ppm,
        duration_ppm: ppm(truth - reported, reported),
        delay_ppm: frequency_ppm,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The two rows that are this board, from p01-pll280's console dump on
    /// Sunday 4 October 2026. `cargo test` is not the parity run, which owns the
    /// whole oracle; these are here so that `cargo test` on its own says
    /// something true rather than nothing.
    #[test]
    fn this_board_as_found_and_after_the_raise() {
        let found = Regs {
            cr: 0x0004_C025,
            cfgr: 0,
            pllckselr: 0x0202_0200,
            pllcfgr: 0x01FF_0000,
            pll1divr: 0x0101_0280,
            cdcfgr1: 0,
            cdcfgr2: 0,
        };
        let t = decode(&found, 64_000_000, 8_000_000);
        assert_eq!(t.refusal, Refusal::Ok);
        assert_eq!(
            (t.sys_hz, t.core_hz, t.ahb_hz, t.pclk1_hz, t.pclk2_hz),
            (64_000_000, 64_000_000, 64_000_000, 64_000_000, 64_000_000)
        );

        let raised = Regs {
            cr: 0x0307_C025,
            cfgr: 0x0000_001B,
            pllckselr: 0x0202_0042,
            pllcfgr: 0x01FF_0004,
            pll1divr: 0x0101_0317,
            cdcfgr1: 0x0000_0008,
            cdcfgr2: 0,
        };
        let t = decode(&raised, 64_000_000, 8_000_000);
        assert_eq!(t.refusal, Refusal::Ok);
        assert_eq!(
            (t.sys_hz, t.core_hz, t.ahb_hz, t.pclk1_hz, t.pclk2_hz),
            (
                280_000_000,
                280_000_000,
                140_000_000,
                140_000_000,
                140_000_000
            )
        );
    }

    /// APB2 is a separate quantity, not a second name for APB1 or the AHB.
    ///
    /// Every row in clock_vectors.json before Tuesday 6 October 2026 held
    /// CDPPRE2 at divide by one, where all three frequencies are equal, so an
    /// implementation that returned ahb_hz or pclk1_hz would have agreed with
    /// all of them. This sets CDPPRE2 to divide by two and leaves CDPPRE1 at
    /// divide by one, so the three answers are 140, 140 and 70 MHz.
    #[test]
    fn apb2_is_not_apb1_and_not_the_ahb() {
        let r = Regs {
            cr: 0x0307_C025,
            cfgr: 0x0000_001B,
            pllckselr: 0x0202_0042,
            pllcfgr: 0x01FF_0004,
            pll1divr: 0x0101_0317,
            cdcfgr1: 0x0000_0008,
            cdcfgr2: 0x0000_0400,
        };
        let t = decode(&r, 64_000_000, 8_000_000);
        assert_eq!(t.refusal, Refusal::Ok);
        assert_eq!(t.ahb_hz, 140_000_000);
        assert_eq!(t.pclk1_hz, 140_000_000);
        assert_eq!(t.pclk2_hz, 70_000_000);
    }

    /// And an undecodable CDPPRE2 refuses, which is the branch added last and
    /// therefore the one most likely to have been left out.
    #[test]
    fn an_undecodable_apb2_prescaler_refuses() {
        let r = Regs {
            cr: 0x0307_C025,
            cfgr: 0x0000_001B,
            pllckselr: 0x0202_0042,
            pllcfgr: 0x01FF_0004,
            pll1divr: 0x0101_0317,
            cdcfgr1: 0,
            cdcfgr2: 0x0000_0600,
        };
        let t = decode(&r, 64_000_000, 8_000_000);
        assert_eq!(t.refusal, Refusal::PrescalerUndecoded);
        assert_eq!(t.pclk2_hz, 0);
    }

    /// Halves away from zero, which is the rule this language gives least help
    /// with, and the asymmetry between the two rows is real: one hertz above
    /// 2 MHz lands on exactly half and one hertz below does not.
    #[test]
    fn halves_round_away_from_zero_and_the_two_rows_are_not_mirrors() {
        let up = bias(2_000_001, 2_000_000);
        assert_eq!((up.frequency_ppm, up.duration_ppm, up.delay_ppm), (1, 0, 1));
        let down = bias(1_999_999, 2_000_000);
        assert_eq!(
            (down.frequency_ppm, down.duration_ppm, down.delay_ppm),
            (-1, 1, -1)
        );
    }

    /// The guard that stops a release build wrapping and a test build panicking.
    #[test]
    fn a_multiply_that_would_leave_32_bits_refuses() {
        let r = Regs {
            cr: 0x0000_0025,
            cfgr: 0x0000_001B,
            pllckselr: 0x0000_0010,
            pllcfgr: 0x0001_0000,
            pll1divr: 0x0000_03FF,
            cdcfgr1: 0,
            cdcfgr2: 0,
        };
        assert_eq!(
            decode(&r, 64_000_000, 8_000_000).refusal,
            Refusal::PllWouldOverflow
        );
    }
}
