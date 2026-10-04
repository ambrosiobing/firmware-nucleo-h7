/* c/clock/clocktree.c: see clocktree.h for why this is not inside system.c.
 *
 * BEHAVIOUR IS UNCHANGED FROM THE FOUR STATIC FUNCTIONS IT REPLACES, and that is
 * a claim worth being careful about, because the whole point of the move is that
 * the board result of Sunday 4 October 2026 still stands afterwards. Every
 * frequency this computes is computed in the same order, with the same integer
 * truncations, from the same masks. The one thing that changed is that a refusal
 * now says which guard fired, where system.c returned one status for all eight.
 *
 * ONE REFUSAL IS MORE SPECIFIC THAN IT WAS, and only in its reason. With PLLSRC
 * set to HSE and HSEBYP clear, system.c took the path where the PLL reference
 * came back 0 and refused there. This reports CLOCKTREE_HSE_NOT_BYPASS instead
 * of a generic unknown source, because that is the true cause and a reader
 * chasing a dead 280 MHz tree wants to be sent to the bypass bit.
 */
#include "clocktree.h"

#include "../board/stm32h7a3_regs.h"

/* The AHB and CPU prescaler encodings. Three of sixteen values are sourced in
 * stm32h7a3_regs.h and the rest are deliberately not: this volume has never
 * written a ratio above 4, and decoding a value it has not read in ST's header
 * would be inventing one. Returning 0 is how the caller refuses. */
static uint32_t ahb_cpu_divider(uint32_t field)
{
    switch (field) {
    case RCC_AHBPRE_DIV1: return 1u;
    case RCC_AHBPRE_DIV2: return 2u;
    case RCC_AHBPRE_DIV4: return 4u;
    default:              return 0u;
    }
}

static uint32_t apb_divider(uint32_t field)
{
    switch (field) {
    case RCC_APBPRE_DIV1: return 1u;
    case RCC_APBPRE_DIV2: return 2u;
    case RCC_APBPRE_DIV4: return 4u;
    default:              return 0u;
    }
}

/* The internal oscillator after its own divider. HSIDIV is two bits and the
 * header names all four values, _1 _2 _4 _8 at field values 0 to 3, so the
 * divisor is 1 << field and this is an exact right shift rather than a rounded
 * division. */
static uint32_t hsi_hz(const clocktree_regs_t *r, uint32_t hsi_nominal)
{
    const uint32_t d = (r->cr & RCC_CR_HSIDIV_MSK) >> RCC_CR_HSIDIV_POS;
    return hsi_nominal >> d;
}

/* The external clock, which is a board fact and not a register fact. With the
 * bypass bit clear the part is waiting on or running from an oscillator this
 * repository knows nothing about, and returning 8 MHz then would assert a board
 * fact that does not apply. */
static uint32_t hse_hz(const clocktree_regs_t *r, uint32_t hse_bypass)
{
    if ((r->cr & RCC_CR_HSEBYP_MSK) == 0u || (r->cr & RCC_CR_HSERDY_MSK) == 0u) {
        return 0u;
    }
    return hse_bypass;
}

/* PLL1's P output, or a reason. DIVM1 holds the value; N1 and P1 hold the value
 * minus one. That asymmetry is ST's and is sourced at RCC_PLL1DIVR_N1_POS.
 *
 * THE ORDER OF THE ARITHMETIC IS PART OF THE ANSWER. The reference is divided by
 * M first and multiplied by N second, so an M that does not divide the reference
 * exactly loses the remainder before the multiply. With the configuration this
 * board runs that is not an issue, because 8 MHz over 4 is exact. It is written
 * down because the opposite order would give a different number for an M of 3,
 * and a second implementation in another language is exactly where that would
 * silently differ. The vector file carries that case for that reason. */
static uint32_t pll1_p_hz(const clocktree_regs_t *r,
                          uint32_t hsi_nominal,
                          uint32_t hse_bypass,
                          clocktree_refusal_t *why)
{
    if ((r->pllcfgr & RCC_PLLCFGR_PLL1FRACEN_MSK) != 0u) {
        *why = CLOCKTREE_PLL_FRACTIONAL;
        return 0u;
    }
    if ((r->pllcfgr & RCC_PLLCFGR_DIVP1EN_MSK) == 0u) {
        *why = CLOCKTREE_PLL_P_DISABLED;
        return 0u;
    }

    uint32_t ref;
    switch (r->pllckselr & RCC_PLLCKSELR_PLLSRC_MSK) {
    case 0u:
        ref = hsi_hz(r, hsi_nominal);
        break;
    case RCC_PLLSRC_HSE:
        ref = hse_hz(r, hse_bypass);
        if (ref == 0u) {
            *why = CLOCKTREE_HSE_NOT_BYPASS;
            return 0u;
        }
        break;
    default:
        *why = CLOCKTREE_PLL_SOURCE_UNKNOWN;
        return 0u;
    }
    if (ref == 0u) {
        *why = CLOCKTREE_PLL_SOURCE_UNKNOWN;
        return 0u;
    }

    const uint32_t m = (r->pllckselr & RCC_PLLCKSELR_DIVM1_MSK) >> RCC_PLLCKSELR_DIVM1_POS;
    const uint32_t n = ((r->pll1divr & RCC_PLL1DIVR_N1_MSK) >> RCC_PLL1DIVR_N1_POS) + 1u;
    const uint32_t p = ((r->pll1divr & RCC_PLL1DIVR_P1_MSK) >> RCC_PLL1DIVR_P1_POS) + 1u;

    if (m == 0u) {
        *why = CLOCKTREE_PLL_ZERO_DIVIDER;
        return 0u;
    }
    const uint32_t in = ref / m;
    if (in == 0u || in > (0xFFFFFFFFu / n)) {
        *why = CLOCKTREE_PLL_WOULD_OVERFLOW;
        return 0u;
    }
    *why = CLOCKTREE_OK;
    return (in * n) / p;
}

void clocktree_decode(const clocktree_regs_t *r,
                      uint32_t hsi_nominal,
                      uint32_t hse_bypass,
                      clocktree_t *out)
{
    out->sys_hz   = 0u;
    out->core_hz  = 0u;
    out->ahb_hz   = 0u;
    out->pclk1_hz = 0u;
    out->refusal  = CLOCKTREE_OK;

    uint32_t sys;
    switch ((r->cfgr & RCC_CFGR_SWS_MSK) >> RCC_CFGR_SWS_POS) {
    case RCC_SWS_HSI:
        sys = hsi_hz(r, hsi_nominal);
        break;
    case RCC_SWS_HSE:
        sys = hse_hz(r, hse_bypass);
        if (sys == 0u) {
            out->refusal = CLOCKTREE_HSE_NOT_BYPASS;
            return;
        }
        break;
    case RCC_SWS_PLL1:
        sys = pll1_p_hz(r, hsi_nominal, hse_bypass, &out->refusal);
        if (sys == 0u) {
            return;
        }
        break;
    default:
        out->refusal = CLOCKTREE_SWS_UNKNOWN;
        return;
    }
    if (sys == 0u) {
        out->refusal = CLOCKTREE_SWS_UNKNOWN;
        return;
    }

    const uint32_t cpu_div =
        ahb_cpu_divider((r->cdcfgr1 & RCC_CDCFGR1_CDCPRE_MSK) >> RCC_CDCFGR1_CDCPRE_POS);
    const uint32_t ahb_div =
        ahb_cpu_divider((r->cdcfgr1 & RCC_CDCFGR1_HPRE_MSK) >> RCC_CDCFGR1_HPRE_POS);
    const uint32_t apb1_div =
        apb_divider((r->cdcfgr2 & RCC_CDCFGR2_CDPPRE1_MSK) >> RCC_CDCFGR2_CDPPRE1_POS);

    if (cpu_div == 0u || ahb_div == 0u || apb1_div == 0u) {
        /* Reporting the undivided frequency here would be wrong by that very
         * ratio, which is the one error a reader would never suspect. */
        out->refusal = CLOCKTREE_PRESCALER_UNDECODED;
        return;
    }

    /* The order of the chain on this part: sys_ck over CDCPRE is the core, the
     * core over HPRE is the AHB buses, the AHB over CDPPRE1 is APB1. */
    out->sys_hz   = sys;
    out->core_hz  = sys / cpu_div;
    out->ahb_hz   = out->core_hz / ahb_div;
    out->pclk1_hz = out->ahb_hz / apb1_div;
    out->refusal  = CLOCKTREE_OK;
}

/* Integer parts per million, halves away from zero, saturating.
 *
 * SATURATION RATHER THAN A REFUSAL, and the reason is that the alternative is
 * worse. A ratio far enough from one to overflow a signed 32 bit count of parts
 * per million is a factor of two thousand, which is not a bias and not anything
 * this instrument is for. Returning the extreme is loud, arithmetically total,
 * and identical in four languages, where a second refusal path would be one more
 * thing for them to disagree about. */
static int32_t ppm(int64_t delta, int64_t den)
{
    const int64_t half   = den / 2;
    const int64_t scaled = delta * 1000000;
    const int64_t q = (delta >= 0) ? ((scaled + half) / den) : ((scaled - half) / den);

    if (q > (int64_t) INT32_MAX) { return INT32_MAX; }
    if (q < (int64_t) INT32_MIN) { return INT32_MIN; }
    return (int32_t) q;
}

const char *clocktree_refusal_text(clocktree_refusal_t why)
{
    switch (why) {
    case CLOCKTREE_OK:                  return "ok";
    case CLOCKTREE_SWS_UNKNOWN:         return "sws-unknown";
    case CLOCKTREE_HSE_NOT_BYPASS:      return "hse-not-bypass";
    case CLOCKTREE_PLL_FRACTIONAL:      return "pll-fractional";
    case CLOCKTREE_PLL_P_DISABLED:      return "pll-p-disabled";
    case CLOCKTREE_PLL_SOURCE_UNKNOWN:  return "pll-source-unknown";
    case CLOCKTREE_PLL_ZERO_DIVIDER:    return "pll-zero-divider";
    case CLOCKTREE_PLL_WOULD_OVERFLOW:  return "pll-would-overflow";
    case CLOCKTREE_PRESCALER_UNDECODED: return "prescaler-undecoded";
    default:                            return "unknown";
    }
}

int clocktree_bias(uint32_t reported_hz, uint32_t true_hz, clocktree_bias_t *out)
{
    out->frequency_ppm = 0;
    out->duration_ppm  = 0;
    out->delay_ppm     = 0;

    if (reported_hz == 0u || true_hz == 0u) {
        return 0;
    }

    const int64_t reported = (int64_t) reported_hz;
    const int64_t truth    = (int64_t) true_hz;

    out->frequency_ppm = ppm(reported - truth, truth);
    out->duration_ppm  = ppm(truth - reported, reported);
    out->delay_ppm     = out->frequency_ppm;   /* the same expression, not a copy */
    return 1;
}
