/* tim6_bringup.c: the sampling timer, moved here unchanged.
 *
 * EVERY LINE BELOW IS THE CODE THAT RAN ON THE BOARD, lifted out of acq_timer.c
 * on Monday 19 October 2026 and not rewritten. See tim6_bringup.h for why the
 * factoring was chosen over duplicating it, and for the criterion this move is
 * judged by: the timer build's console output must come back byte-identical,
 * because a pure move changes no register write and no format string.
 *
 * One thing changed and it is the one thing a shared function must change: the
 * rate is a parameter rather than ACQ_RATE_HZ compiled in. The timer back end
 * passes ACQ_RATE_HZ, so its behaviour is identical.
 */
#include "tim6_bringup.h"

#include "acq_errors.h"
#include "adc_bringup.h"          /* apb1_divisor(), shared since Friday 9 October 2026 */

#include "board.h"
#include "pwmmath.h"

#include "stm32h7xx.h"

#include <stdbool.h>
#include <stdio.h>

int tim6_bring_up(uint32_t rate_hz)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM6EN;
    (void) RCC->APB1LENR;              /* the write is posted; read it back */

    /* TIM6 at exactly the requested rate, and the timer clock is derived rather
     * than refused.
     *
     * THIS BLOCK USED TO RETURN -2 UNCONDITIONALLY, with a comment saying the
     * APB1 timer clock was unconfirmed and that whether the timer clock equals
     * the bus clock or twice it was the factor between 1 kHz and 2 kHz. That is
     * exactly the question pwmmath_timer_hz answers, from ST's own
     * stm32h7xx_hal_rcc_ex.h lines 3596 to 3605, sourced Tuesday 6 October 2026:
     * the timer kernel clock is min(HCLK, 2 x PCLK) with TIMPRE clear and
     * min(HCLK, 4 x PCLK) with it set. Fourteen cases of that rule are checked
     * on the host against a second derivation, so the factor is settled and no
     * longer guessed.
     *
     * Three things can still refuse, and each says which. */
    const uint32_t pclk1 = board_pclk1_hz();
    if (pclk1 == 0u) {
        /* The clock tree was not decoded, so there is no honest frequency to
         * plan against. */
        return ACQ_ERR_PCLK1_UNKNOWN;
    }

    /* TIMPRE read rather than assumed. Nothing in this repository writes the
     * bit, so it is whatever reset left, which is clear; reading it costs one
     * load and means this does not depend on that staying true. */
    const bool timpre = (RCC->CFGR & RCC_CFGR_TIMPRE) != 0u;
    const uint32_t timer_clk_hz = pwmmath_timer_hz(pclk1, apb1_divisor(), timpre);
    if (timer_clk_hz == 0u) {
        /* Either CDPPRE1 holds a ratio this volume has not sourced, or the
         * rule does not cover the combination. Refusing is correct: a build
         * that guessed the clock would produce a clean looking square wave at
         * the wrong rate, and the whole point of chapter 6 is that such a wave
         * is indistinguishable from a right one without an external witness. */
        return ACQ_ERR_APB1_PRESCALER;
    }

    printf("  tim6 clock            %lu Hz  (pclk1 %lu, /%lu, timpre %u)\r\n",
           (unsigned long) timer_clk_hz, (unsigned long) pclk1,
           (unsigned long) apb1_divisor(), (unsigned) timpre);

    const uint32_t ticks = timer_clk_hz / rate_hz;
    uint32_t psc = 0u;
    uint32_t arr = ticks;
    while (arr > 0xFFFFu) {            /* TIM6 is a 16-bit counter */
        psc++;
        arr = ticks / (psc + 1u);
    }

    TIM6->CR1 = 0u;
    TIM6->PSC = psc;
    TIM6->ARR = arr - 1u;
    TIM6->CNT = 0u;
    TIM6->EGR = TIM_EGR_UG;            /* load the prescaler immediately */

    /* Trigger output on the update event. This is the line that makes the
     * timing a hardware property. */
    TIM6->CR2 = (TIM6->CR2 & ~TIM_CR2_MMS) | (2u << TIM_CR2_MMS_Pos);

    return 0;
}

void tim6_start(void)
{
    TIM6->CR1 |= TIM_CR1_CEN;
    printf("  tim6 started, so conversions begin on its update event\r\n");
}
