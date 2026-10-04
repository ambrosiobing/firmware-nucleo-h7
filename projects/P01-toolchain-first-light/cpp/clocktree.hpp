// P01's clock tree decode in C++, held to the same oracle as the C.
//
// WHAT IS DELIBERATELY THE SAME. Every frequency, every refusal, every
// truncation and the rounding rule for parts per million. Those are the
// specification, they live in ../clock_vectors.json, and a difference in any of
// them is a defect in one of the four implementations rather than a language
// difference worth writing up.
//
// WHAT IS DELIBERATELY DIFFERENT, because the comparison is only interesting if
// each language is allowed to be itself:
//
//   - the refusal is an `enum class`, so a refusal cannot be compared with an
//     integer or with a frequency by accident. The C uses a plain enum because
//     C has no alternative and because clocktree_refusal_text takes an int in
//     order to be callable from the board's printf path.
//   - the results come back by value in a struct rather than through an out
//     parameter. The C returns through a pointer because the firmware calls it
//     from SystemInit, where every byte of stack is accounted for and a struct
//     return is a copy nobody asked for.
//   - `std::string_view` for the refusal token, which borrows the same string
//     literals and allocates nothing.
//
// WHAT IT MUST NOT DO. No exceptions, no RTTI, no heap: this is built with
// -fno-exceptions -fno-rtti, as the firmware C++ is, so that the same file could
// be linked into an image. It is not linked into one today, and P01's cpp README
// says so rather than implying otherwise.
//
// HEADER ONLY, which is a decision about this project and not a style. There is
// one translation unit that uses it, clocktree_filter.cpp, and a separate .cpp
// would add a build step and a declaration to keep in step with a definition for
// no gain. P09's cpp/payload.hpp is header-only for the same reason; P12's
// gates.hpp is too.
#ifndef P01_CLOCKTREE_HPP
#define P01_CLOCKTREE_HPP

#include <cstdint>
#include <string_view>

namespace p01 {

// The seven words the decode reads, named for their registers so a console dump
// maps to them by eye.
struct Regs {
    std::uint32_t cr{};
    std::uint32_t cfgr{};
    std::uint32_t pllckselr{};
    std::uint32_t pllcfgr{};
    std::uint32_t pll1divr{};
    std::uint32_t cdcfgr1{};
    std::uint32_t cdcfgr2{};
};

// The eight ways this can give up, in the order they are tested. The values
// match the C's enum because the oracle compares the TOKENS and a mismatched
// integer would still be caught, but matching them means a reader comparing the
// two files is not asked to hold two orderings in mind.
enum class Refusal : int {
    Ok = 0,
    SwsUnknown = 1,
    HseNotBypass = 2,
    PllFractional = 3,
    PllPDisabled = 4,
    PllSourceUnknown = 5,
    PllZeroDivider = 6,
    PllWouldOverflow = 7,
    PrescalerUndecoded = 8,
};

struct Tree {
    std::uint32_t sys_hz{};
    std::uint32_t core_hz{};
    std::uint32_t ahb_hz{};
    std::uint32_t pclk1_hz{};
    Refusal refusal{Refusal::Ok};
};

struct Bias {
    bool ok{};
    std::int32_t frequency_ppm{};
    std::int32_t duration_ppm{};
    std::int32_t delay_ppm{};
};

// The field positions. Written out here rather than included from
// c/board/stm32h7a3_regs.h, and that is the one place this file deliberately
// duplicates rather than shares.
//
// The reason is the point of the exercise. A second implementation that includes
// the first one's constants cannot disagree with it about where a field sits, so
// it cannot catch the error it exists to catch: that DIVM1 holds the value while
// N1 and P1 hold the value minus one. Sharing the header would make these four
// implementations one implementation with three front ends. The authority for
// every number below is the same as the C's, named at each define in
// c/board/stm32h7a3_regs.h: ST's CMSIS device header for this die.
namespace field {
constexpr std::uint32_t CR_HSIDIV_MSK = 3u << 3;
constexpr unsigned CR_HSIDIV_POS = 3;
constexpr std::uint32_t CR_HSEBYP_MSK = 1u << 18;
constexpr std::uint32_t CR_HSERDY_MSK = 1u << 17;

constexpr std::uint32_t CFGR_SWS_MSK = 7u << 3;
constexpr unsigned CFGR_SWS_POS = 3;
constexpr std::uint32_t SWS_HSI = 0;
constexpr std::uint32_t SWS_HSE = 2;
constexpr std::uint32_t SWS_PLL1 = 3;

constexpr std::uint32_t PLLCKSELR_PLLSRC_MSK = 3u << 0;
constexpr std::uint32_t PLLSRC_HSE = 2;
constexpr std::uint32_t PLLCKSELR_DIVM1_MSK = 0x3Fu << 4;
constexpr unsigned PLLCKSELR_DIVM1_POS = 4;

constexpr std::uint32_t PLLCFGR_PLL1FRACEN_MSK = 1u << 0;
constexpr std::uint32_t PLLCFGR_DIVP1EN_MSK = 1u << 16;

constexpr std::uint32_t PLL1DIVR_N1_MSK = 0x1FFu << 0;
constexpr unsigned PLL1DIVR_N1_POS = 0;
constexpr std::uint32_t PLL1DIVR_P1_MSK = 0x7Fu << 9;
constexpr unsigned PLL1DIVR_P1_POS = 9;

constexpr std::uint32_t CDCFGR1_HPRE_MSK = 0xFu << 0;
constexpr unsigned CDCFGR1_HPRE_POS = 0;
constexpr std::uint32_t CDCFGR1_CDCPRE_MSK = 0xFu << 8;
constexpr unsigned CDCFGR1_CDCPRE_POS = 8;
constexpr std::uint32_t CDCFGR2_CDPPRE1_MSK = 7u << 4;
constexpr unsigned CDCFGR2_CDPPRE1_POS = 4;

constexpr std::uint32_t AHBPRE_DIV1 = 0x0;
constexpr std::uint32_t AHBPRE_DIV2 = 0x8;
constexpr std::uint32_t AHBPRE_DIV4 = 0x9;
constexpr std::uint32_t APBPRE_DIV1 = 0x0;
constexpr std::uint32_t APBPRE_DIV2 = 0x4;
constexpr std::uint32_t APBPRE_DIV4 = 0x5;
}  // namespace field

constexpr std::string_view refusal_text(Refusal why) noexcept
{
    switch (why) {
    case Refusal::Ok:                 return "ok";
    case Refusal::SwsUnknown:         return "sws-unknown";
    case Refusal::HseNotBypass:       return "hse-not-bypass";
    case Refusal::PllFractional:      return "pll-fractional";
    case Refusal::PllPDisabled:       return "pll-p-disabled";
    case Refusal::PllSourceUnknown:   return "pll-source-unknown";
    case Refusal::PllZeroDivider:     return "pll-zero-divider";
    case Refusal::PllWouldOverflow:   return "pll-would-overflow";
    case Refusal::PrescalerUndecoded: return "prescaler-undecoded";
    }
    return "unknown";
}

namespace detail {

// Three of sixteen encodings are sourced and the rest are not, because the
// remaining ratios are not evenly spaced. 0 means the caller must refuse.
constexpr std::uint32_t ahb_cpu_divider(std::uint32_t f) noexcept
{
    switch (f) {
    case field::AHBPRE_DIV1: return 1;
    case field::AHBPRE_DIV2: return 2;
    case field::AHBPRE_DIV4: return 4;
    default:                 return 0;
    }
}

constexpr std::uint32_t apb_divider(std::uint32_t f) noexcept
{
    switch (f) {
    case field::APBPRE_DIV1: return 1;
    case field::APBPRE_DIV2: return 2;
    case field::APBPRE_DIV4: return 4;
    default:                 return 0;
    }
}

// HSIDIV is two bits and the divisor is 1 << field, so this is an exact shift
// and not a rounded division.
constexpr std::uint32_t hsi_hz(const Regs &r, std::uint32_t nominal) noexcept
{
    return nominal >> ((r.cr & field::CR_HSIDIV_MSK) >> field::CR_HSIDIV_POS);
}

// A board fact and not a register fact: with the bypass bit clear, the part is
// running from an oscillator this repository knows nothing about.
constexpr std::uint32_t hse_hz(const Regs &r, std::uint32_t bypass) noexcept
{
    if ((r.cr & field::CR_HSEBYP_MSK) == 0 || (r.cr & field::CR_HSERDY_MSK) == 0) {
        return 0;
    }
    return bypass;
}

// PLL1's P output, or a reason.
//
// THE ORDER OF THE ARITHMETIC IS PART OF THE ANSWER: the reference is divided by
// M first and multiplied by N second, so an M that does not divide exactly loses
// the remainder before the multiply. The oracle has the M of 3 case for exactly
// this, because the opposite order is just as plausible to write and gives a
// different number.
constexpr std::uint32_t pll1_p_hz(const Regs &r, std::uint32_t hsi_nominal,
                                  std::uint32_t hse_bypass, Refusal &why) noexcept
{
    if ((r.pllcfgr & field::PLLCFGR_PLL1FRACEN_MSK) != 0) {
        why = Refusal::PllFractional;
        return 0;
    }
    if ((r.pllcfgr & field::PLLCFGR_DIVP1EN_MSK) == 0) {
        why = Refusal::PllPDisabled;
        return 0;
    }

    std::uint32_t ref = 0;
    switch (r.pllckselr & field::PLLCKSELR_PLLSRC_MSK) {
    case 0:
        ref = hsi_hz(r, hsi_nominal);
        break;
    case field::PLLSRC_HSE:
        ref = hse_hz(r, hse_bypass);
        if (ref == 0) {
            why = Refusal::HseNotBypass;
            return 0;
        }
        break;
    default:
        why = Refusal::PllSourceUnknown;
        return 0;
    }
    if (ref == 0) {
        why = Refusal::PllSourceUnknown;
        return 0;
    }

    // DIVM1 holds the value; N1 and P1 hold the value minus one. ST's asymmetry,
    // not a transcription error, and the single most likely thing for a second
    // implementation to get wrong.
    const std::uint32_t m =
        (r.pllckselr & field::PLLCKSELR_DIVM1_MSK) >> field::PLLCKSELR_DIVM1_POS;
    const std::uint32_t n =
        ((r.pll1divr & field::PLL1DIVR_N1_MSK) >> field::PLL1DIVR_N1_POS) + 1;
    const std::uint32_t p =
        ((r.pll1divr & field::PLL1DIVR_P1_MSK) >> field::PLL1DIVR_P1_POS) + 1;

    if (m == 0) {
        why = Refusal::PllZeroDivider;
        return 0;
    }
    const std::uint32_t in = ref / m;
    if (in == 0 || in > (0xFFFFFFFFu / n)) {
        why = Refusal::PllWouldOverflow;
        return 0;
    }
    why = Refusal::Ok;
    return (in * n) / p;
}

// Parts per million, halves away from zero, saturating at the signed 32-bit
// bounds. The intermediate must be 64-bit: 327178 times a million is 3.3e14.
//
// Saturation rather than a second refusal, so there is one refusal path in all
// four languages and not two things for them to disagree about.
constexpr std::int32_t ppm(std::int64_t delta, std::int64_t den) noexcept
{
    const std::int64_t half = den / 2;
    const std::int64_t scaled = delta * 1000000;
    const std::int64_t q = (delta >= 0) ? ((scaled + half) / den)
                                        : ((scaled - half) / den);
    if (q > static_cast<std::int64_t>(INT32_MAX)) { return INT32_MAX; }
    if (q < static_cast<std::int64_t>(INT32_MIN)) { return INT32_MIN; }
    return static_cast<std::int32_t>(q);
}

}  // namespace detail

constexpr Tree decode(const Regs &r, std::uint32_t hsi_nominal,
                      std::uint32_t hse_bypass) noexcept
{
    Tree out{};

    std::uint32_t sys = 0;
    switch ((r.cfgr & field::CFGR_SWS_MSK) >> field::CFGR_SWS_POS) {
    case field::SWS_HSI:
        sys = detail::hsi_hz(r, hsi_nominal);
        break;
    case field::SWS_HSE:
        sys = detail::hse_hz(r, hse_bypass);
        if (sys == 0) {
            out.refusal = Refusal::HseNotBypass;
            return out;
        }
        break;
    case field::SWS_PLL1:
        sys = detail::pll1_p_hz(r, hsi_nominal, hse_bypass, out.refusal);
        if (sys == 0) {
            return out;
        }
        break;
    default:
        out.refusal = Refusal::SwsUnknown;
        return out;
    }
    if (sys == 0) {
        out.refusal = Refusal::SwsUnknown;
        return out;
    }

    const std::uint32_t cpu_div = detail::ahb_cpu_divider(
        (r.cdcfgr1 & field::CDCFGR1_CDCPRE_MSK) >> field::CDCFGR1_CDCPRE_POS);
    const std::uint32_t ahb_div = detail::ahb_cpu_divider(
        (r.cdcfgr1 & field::CDCFGR1_HPRE_MSK) >> field::CDCFGR1_HPRE_POS);
    const std::uint32_t apb1_div = detail::apb_divider(
        (r.cdcfgr2 & field::CDCFGR2_CDPPRE1_MSK) >> field::CDCFGR2_CDPPRE1_POS);

    if (cpu_div == 0 || ahb_div == 0 || apb1_div == 0) {
        // Reporting the undivided frequency would be wrong by that very ratio,
        // which is the one error nobody would suspect.
        out.refusal = Refusal::PrescalerUndecoded;
        return out;
    }

    // sys_ck over CDCPRE is the core, the core over HPRE is the AHB buses, the
    // AHB over CDPPRE1 is APB1.
    out.sys_hz = sys;
    out.core_hz = sys / cpu_div;
    out.ahb_hz = out.core_hz / ahb_div;
    out.pclk1_hz = out.ahb_hz / apb1_div;
    out.refusal = Refusal::Ok;
    return out;
}

// delay_ppm equals frequency_ppm by the same expression and not by assignment of
// a copy: both divide by the true frequency. duration_ppm divides by the
// reported one, so it differs in the last unit or two, and knowing these are two
// quantities rather than three is most of the value here.
constexpr Bias bias(std::uint32_t reported_hz, std::uint32_t true_hz) noexcept
{
    Bias out{};
    if (reported_hz == 0 || true_hz == 0) {
        return out;
    }
    const std::int64_t reported = static_cast<std::int64_t>(reported_hz);
    const std::int64_t truth = static_cast<std::int64_t>(true_hz);

    out.ok = true;
    out.frequency_ppm = detail::ppm(reported - truth, truth);
    out.duration_ppm = detail::ppm(truth - reported, reported);
    out.delay_ppm = out.frequency_ppm;
    return out;
}

}  // namespace p01

#endif  // P01_CLOCKTREE_HPP
