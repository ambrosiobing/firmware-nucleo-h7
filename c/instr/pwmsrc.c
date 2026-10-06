/* c/instr/pwmsrc.c: TIM1 channel 3 on PE13 at a known frequency.
 *
 * pwmsrc.h carries the design, why a self test was rejected for a feedback
 * loop, and the one register fact that would otherwise cost a bench session.
 * This file is only the register work; every calculation is in pwmmath.c.
 */
#include "pwmsrc.h"

#include "pwmmath.h"
#include "../board/board.h"
#include "../board/stm32h7a3_regs.h"

#ifdef BOARD_REGS_CONFIRMED

/* The APB2 prescaler as a divisor, from the three encodings this volume has
 * sourced out of ST's header. Anything else returns 0, which pwmmath_timer_hz
 * then refuses, and that refusal is correct rather than unfortunate: reporting
 * an undivided frequency here would be wrong by exactly the ratio nobody would
 * suspect. The same argument clocktree.c makes for the other three prescalers.
 */
static uint32_t apb2_divisor(void)
{
    const uint32_t field =
        (RCC_CDCFGR2 & RCC_CDCFGR2_CDPPRE2_MSK) >> RCC_CDCFGR2_CDPPRE2_POS;
    switch (field) {
    case RCC_APBPRE_DIV1: return 1u;
    case RCC_APBPRE_DIV2: return 2u;
    case RCC_APBPRE_DIV4: return 4u;
    default:              return 0u;
    }
}


int pwmsrc_start(uint32_t want_hz, pwmsrc_t *out)
{
    if (out == 0) {
        return PWMSRC_ERR_PLAN;
    }

    out->tim_hz = 0u;
    out->want_hz = want_hz;
    out->psc = 0u;
    out->arr = 0u;
    out->ccr = 0u;
    out->achieved_mhz = 0u;
    out->error_ppm = 0;

    /* The bus clock the timer is on, which has to be established before any of
     * the arithmetic means anything. 0 means the clock tree was not decoded, so
     * there is no honest frequency to plan against. */
    const uint32_t pclk2 = board_pclk2_hz();
    if (pclk2 == 0u) {
        return PWMSRC_ERR_PCLK2_UNKNOWN;
    }

    /* TIMPRE read rather than assumed, since Tuesday 6 October 2026 when the
     * rule behind it was sourced from ST's pack. Nothing in this repository
     * writes the bit, so it is whatever reset left, which is clear; reading it
     * costs one load and means this does not depend on that staying true. */
    const bool timpre = (RCC_CFGR & RCC_CFGR_TIMPRE_MSK) != 0u;
    const uint32_t tim_hz = pwmmath_timer_hz(pclk2, apb2_divisor(), timpre);
    if (tim_hz == 0u) {
        /* Either CDPPRE2 holds a ratio this volume has not sourced, or it
         * divides by more than one and the TIMPRE half of the rule is unread.
         * pwmsrc.h says why that is a refusal and not a defect. */
        return PWMSRC_ERR_APB2_PRESCALER;
    }
    out->tim_hz = tim_hz;

    struct pwmmath_plan plan;
    if (pwmmath_plan(tim_hz, want_hz, &plan) != PWMMATH_OK) {
        return PWMSRC_ERR_PLAN;
    }
    out->psc = plan.psc;
    out->arr = plan.arr;
    out->ccr = plan.ccr;
    out->achieved_mhz = plan.achieved_mhz;
    out->error_ppm = pwmmath_error_ppm(plan.achieved_mhz, want_hz);

    /* The peripheral's bus clock and the port's, in that order and for the same
     * reason freqcount_init gives: every write below goes nowhere without the
     * first, and the pin configuration goes nowhere without the second. */
    RCC_APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void) RCC_APB2ENR;
    RCC_AHB4ENR |= RCC_AHB4ENR_GPIOEEN;
    (void) RCC_AHB4ENR;

    /* THE PIN, without which every register below is correct and nothing
     * reaches the wire. PE13 to alternate function 1, which is TIM1_CH3 on this
     * part, from ST's TIM_DMA example for this exact board. Push-pull with a
     * pull-up, matching that example: the pull-up does nothing while the timer
     * drives the line and holds it defined when pwmsrc_stop clears MOE.
     *
     * board_pin_alternate is the shared helper rather than a local copy,
     * because the rule it encodes is worth having once: AFRL covers pins 0 to 7
     * and AFRH covers 8 to 15, and pin 13 is in the second. freqcount.c calling
     * it for PD12 is what made it public on Monday 5 October 2026. */
    board_pin_alternate(PWMSRC_OUT_PORT, PWMSRC_OUT_PIN, PWMSRC_OUT_AF,
                        GPIO_PUPD_PULLUP);

    /* Counter off while it is configured. A running counter with a half written
     * configuration would put an arbitrary frequency on the pin, which the
     * counter at the other end of the wire would faithfully report. */
    TIM1_CR1 = 0u;
    (void) TIM1_CR1;

    TIM1_PSC = plan.psc;
    TIM1_ARR = plan.arr;
    TIM1_CCR3 = plan.ccr;
    TIM1_RCR = 0u;

    /* PWM mode 1 on channel 3, with the compare preloaded. OC3M occupies bits
     * 6:4 with a fourth bit at 16, which is why the mask is not a plain
     * three-bit field; stm32h7a3_regs.h records that beside the definition.
     * Channel 3 uses the LOW byte of CCMR2, the way channel 1 uses the low byte
     * of CCMR1. */
    uint32_t ccmr2 = TIM1_CCMR2;
    ccmr2 &= ~(TIM_CCMR2_OC3M_MSK | TIM_CCMR2_OC3PE_MSK);
    ccmr2 |= TIM_CCMR2_OC3M_PWM1 | TIM_CCMR2_OC3PE_MSK;
    TIM1_CCMR2 = ccmr2;

    /* Output enabled, active high. CC3P is cleared explicitly rather than left
     * alone: the polarity does not change the frequency, so a stale 1 here would
     * be invisible at the counter and visible on an oscilloscope, which is the
     * kind of difference that wastes an afternoon later. */
    uint32_t ccer = TIM1_CCER;
    ccer &= ~TIM_CCER_CC3P_MSK;
    ccer |= TIM_CCER_CC3E_MSK;
    TIM1_CCER = ccer;

    /* Autoreload preloaded, then an update event to transfer ARR and CCR3 from
     * their preload registers into the shadow registers the counter actually
     * uses. Without the update the first period runs on whatever the shadow
     * registers held, which on a fresh peripheral is 0 and on a reconfigured one
     * is the previous frequency. */
    TIM1_CR1 |= TIM_CR1_ARPE_MSK;
    TIM1_EGR = TIM_EGR_UG_MSK;

    /* Read back before enabling anything that drives the pin. A wrong base
     * address fails exactly here, and this is the one failure that would
     * otherwise look like a signal of zero hertz at the other end of the wire,
     * which is indistinguishable from no wire at all. */
    if (TIM1_PSC != (uint32_t) plan.psc
        || TIM1_ARR != (uint32_t) plan.arr
        || TIM1_CCR3 != (uint32_t) plan.ccr
        || (TIM1_CCMR2 & TIM_CCMR2_OC3M_MSK) != TIM_CCMR2_OC3M_PWM1
        || (TIM1_CCER & TIM_CCER_CC3E_MSK) == 0u) {
        return PWMSRC_ERR_READBACK;
    }

    /* AND THE BIT WITHOUT WHICH EVERYTHING ABOVE IS SILENT. TIM1 is an
     * advanced-control timer and its outputs are gated by MOE. Every general
     * purpose timer example omits this because those timers have no such bit. */
    TIM1_BDTR |= TIM_BDTR_MOE_MSK;

    TIM1_CR1 |= TIM_CR1_CEN_MSK;

    if ((TIM1_CR1 & TIM_CR1_CEN_MSK) == 0u
        || (TIM1_BDTR & TIM_BDTR_MOE_MSK) == 0u) {
        return PWMSRC_ERR_READBACK;
    }

    return PWMSRC_OK;
}


int pwmsrc_stop(void)
{
    TIM1_CR1 &= ~TIM_CR1_CEN_MSK;
    TIM1_BDTR &= ~TIM_BDTR_MOE_MSK;

    if ((TIM1_CR1 & TIM_CR1_CEN_MSK) != 0u
        || (TIM1_BDTR & TIM_BDTR_MOE_MSK) != 0u) {
        return PWMSRC_ERR_READBACK;
    }
    /* The pin is deliberately left configured. pwmsrc.h says why: a pin returned
     * to its reset state floats, and a floating line into the counter reads as
     * an arbitrary frequency rather than as nothing. */
    return PWMSRC_OK;
}

#else   /* the register addresses are not confirmed for this part */

int pwmsrc_start(uint32_t want_hz, pwmsrc_t *out)
{
    if (out != 0) {
        /* Zeroed rather than planned, and the distinction is worth stating
         * because freqcount.c's unconfirmed branch does the opposite.
         * freqcount_resolution_mhz still answers properly there, because a
         * resolution is a function of the gate alone and needs no register. A
         * plan is not: it needs the timer clock, which comes from
         * board_pclk2_hz(), which is 0 until the clock tree has been decoded
         * out of registers this build does not have. So there is nothing
         * honest to report except the target that was asked for. */
        out->tim_hz = 0u;
        out->want_hz = want_hz;
        out->psc = 0u;
        out->arr = 0u;
        out->ccr = 0u;
        out->achieved_mhz = 0u;
        out->error_ppm = 0;
    }
    return PWMSRC_ERR_REGS;
}

int pwmsrc_stop(void)
{
    return PWMSRC_ERR_REGS;
}

#endif  /* BOARD_REGS_CONFIRMED */


const char *pwmsrc_refusal_text(int code)
{
    switch (code) {
    case PWMSRC_OK:                   return "ok";
    case PWMSRC_ERR_REGS:             return "regs-unconfirmed";
    case PWMSRC_ERR_PCLK2_UNKNOWN:    return "apb2-clock-unknown";
    case PWMSRC_ERR_APB2_PRESCALER:   return "apb2-prescaler-undecoded";
    case PWMSRC_ERR_PLAN:             return "target-unreachable";
    case PWMSRC_ERR_READBACK:         return "readback-mismatch";
    default:                          return "unknown-refusal";
    }
}
