/* tim3_bringup.c: the sampling timer, and the marker it now drives in hardware.
 *
 * THE COUNTER HALF IS tim6_bringup.c MOVED ACROSS, register for register, with
 * TIM6 spelled TIM3 and its clock enable changed. That half ran on the board
 * from Wednesday 7 October 2026 inside acq_timer.c and from Monday 19 October
 * 2026 as a shared function, and it is not rewritten here.
 *
 * THE CRITERION FOR THE COUNTER HALF, published before the run as every move in
 * this project is: the timer clock line and the prescaler plan must produce the
 * same cadence as the TIM6 runs, which the witness tests directly, and the
 * configuration read-backs must differ from the TIM6 captures in nothing but the
 * register names. TIM3 and TIM6 sit on the same bus and take the same kernel
 * clock, so a different rate would mean the plan, not the timer.
 *
 * THE CHANNEL HALF IS NEW, and it is four register writes: a mode, a compare
 * value, an output enable and the pad's alternate function. What it buys is that
 * the marker edge is produced by the same counter that triggers the conversion,
 * so the two are separated by a fixed number of timer ticks instead of by
 * whatever the processor happened to be doing.
 *
 * WHY TOGGLE MODE AND A COMPARE VALUE OF ZERO. Toggle leaves the pin where the
 * match put it until the next match, so the witness sees ONE EDGE PER SAMPLING
 * INSTANT and counts both polarities, which is exactly what marker_toggle()
 * produced in software and what MEASUREMENT.md and the analysers have assumed
 * since Friday 9 October 2026. A pulse would be tens of nanoseconds wide and all
 * but invisible to a sampler whose period is 10 microseconds: that is not a
 * guess, it is the measured result of the first capture, which caught fragments
 * of a pulse in 346 samples out of 199 784 and reported a rate of 50.67 Hz. See
 * marker.c for that record.
 *
 * The compare value is zero so the match happens at the counter's zero, which is
 * also where the update event and therefore the conversion trigger are. One
 * thing is NOT claimed here: whether the match at zero and the update event land
 * on the same timer cycle or one apart. Nothing read so far settles it, and the
 * measurement does not depend on it, because a fixed offset common to every
 * period changes no interval between edges. If it ever matters, it is a scope
 * question and not a console question.
 *
 * EVERY NAME IN THIS FILE WAS VERIFIED BEFORE IT WAS WRITTEN, on a laptop with
 * no compiler, which is the discipline this project adopted after a missing
 * include cost a round trip to another machine. Thirteen names were checked
 * against the part's CMSIS device header: RCC_APB1LENR_TIM3EN, RCC_CFGR_TIMPRE,
 * TIM_CR1_CEN, TIM_CR2_MMS and its _Pos, TIM_EGR_UG, TIM_CCMR1_CC1S,
 * TIM_CCMR1_OC1M and its _Pos, TIM_CCMR1_OC1PE, TIM_CCER_CC1E, TIM_CCER_CC1P,
 * and RCC_AHB4ENR_GPIOBEN for marker.c. TIM_TypeDef was confirmed to carry
 * CR1, CR2, PSC, ARR, CNT, EGR, CCMR1, CCR1 and CCER, and GPIO_TypeDef to carry
 * AFR as a two-element array, which is what makes marker.c's AFR[0] the
 * register for pins 0 to 7.
 *
 * TWO CONTROLS, because a check that cannot fail proves nothing. A deliberately
 * invented name, TIM_CCMR1_OC1M_TOGGLE, returned nothing, so the search was
 * capable of reporting an absence. And five of the thirteen are names this
 * project already compiles on the board, so the header read is the header the
 * build sees in every respect that matters here.
 *
 * THE COPY READ WAS NOT THE PACK, and that is stated rather than glossed. It was
 * STMicroelectronics/cmsis_device_h7, branch master, Include/stm32h7a3xxq.h; the
 * build uses the same file from STM32Cube_FW_H7_V1.13.0 on win11 skyhorizon, so
 * line numbers from this reading would not match that pack and none are quoted.
 * The names are what was checked and the names are what the compiler needs.
 *
 * WHY THERE IS NO MAIN OUTPUT ENABLE HERE, which is the trap in the opposite
 * direction and is written down in c/board/stm32h7a3_regs.h: TIM1 is an
 * advanced-control timer whose outputs are gated by MOE in BDTR, so every other
 * register can be right and the pin stays where the GPIO configuration left it.
 * The general-purpose timers have no such bit. TIM3 is general-purpose, so CC1E
 * is the whole of the output enable, and a reader coming from a TIM1 example
 * will look for a BDTR write that must not be there.
 */
#include "tim3_bringup.h"

#include "acq_errors.h"
#include "adc_bringup.h"          /* apb1_divisor(), shared since Friday 9 October 2026 */
#include "marker.h"               /* the pad, which marker.c owns */

#include "board.h"
#include "pwmmath.h"

#include "stm32h7xx.h"

#include <stdbool.h>
#include <stdio.h>

/* One line per channel register, in the shape the converter's steps print in.
 *
 * NOT adc_report(), and the reason is in that function's format string rather
 * than in taste: it prints a literal "adc " in front of the step name, so a
 * timer register reported through it would read "adc marker channel mode" and
 * attribute a TIM3 register to the converter. A reader of a console capture has
 * nothing but these lines to go on. */
static void tim3_report(const char *step, uint32_t wrote, uint32_t read_back,
                        bool ok)
{
    printf("  tim3 %-21s %s  wrote %08lX  read back %08lX\r\n",
           step, ok ? "ok     " : "REFUSED",
           (unsigned long) wrote, (unsigned long) read_back);
}

int tim3_bring_up(uint32_t rate_hz)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM3EN;
    (void) RCC->APB1LENR;              /* the write is posted; read it back */

    /* TIM3 at exactly the requested rate, and the timer clock is derived rather
     * than refused.
     *
     * pwmmath_timer_hz answers the question the old inline block refused: ST's
     * own stm32h7xx_hal_rcc_ex.h lines 3596 to 3605, sourced Tuesday 6 October
     * 2026, give the timer kernel clock as min(HCLK, 2 x PCLK) with TIMPRE clear
     * and min(HCLK, 4 x PCLK) with it set. Fourteen cases of that rule are
     * checked on the host against a second derivation, so the factor between
     * 1 kHz and 2 kHz is settled and no longer guessed.
     *
     * TIM3 IS ON THE SAME BUS AS TIM6, both in RCC_APB1LENR, so this derivation
     * is the same derivation and not a new claim: TIM3EN is bit 1 and TIM6EN is
     * bit 4 of one register. That is why the cadence is predicted not to move.
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
        /* Either CDPPRE1 holds a ratio this volume has not sourced, or the rule
         * does not cover the combination. Refusing is correct: a build that
         * guessed the clock would produce a clean looking square wave at the
         * wrong rate, and the whole point of chapter 6 is that such a wave is
         * indistinguishable from a right one without an external witness. */
        return ACQ_ERR_APB1_PRESCALER;
    }

    printf("  tim3 clock            %lu Hz  (pclk1 %lu, /%lu, timpre %u)\r\n",
           (unsigned long) timer_clk_hz, (unsigned long) pclk1,
           (unsigned long) apb1_divisor(), (unsigned) timpre);

    /* The prescaler plan, carried across unchanged. ARR is kept inside 16 bits,
     * which is required if this counter is 16 bits wide and harmless if it is
     * wider; whether TIM3 on this part counts 16 or 32 is not claimed here,
     * because a plan that fits 16 bits fits both.
     *
     * ONE HOLE IS NAMED RATHER THAN CLOSED, so that this stays the code that
     * ran. A rate_hz above the timer clock makes ticks zero and arr - 1 wrap to
     * 0xFFFFFFFF, which would configure a cadence nothing asked for. It cannot
     * fire for the only caller, which passes ACQ_RATE_HZ at 1000 against a
     * timer clock in the tens of megahertz, and closing it would make this a
     * rewrite rather than a move. It is written here so the next caller sees
     * it. */
    const uint32_t ticks = timer_clk_hz / rate_hz;
    uint32_t psc = 0u;
    uint32_t arr = ticks;
    while (arr > 0xFFFFu) {
        psc++;
        arr = ticks / (psc + 1u);
    }

    TIM3->CR1 = 0u;
    TIM3->PSC = psc;
    TIM3->ARR = arr - 1u;
    TIM3->CNT = 0u;
    TIM3->EGR = TIM_EGR_UG;            /* load the prescaler immediately */

    /* Trigger output on the update event. This is the line that makes the
     * timing a hardware property. */
    TIM3->CR2 = (TIM3->CR2 & ~TIM_CR2_MMS) | (2u << TIM_CR2_MMS_Pos);

    /* THE MARKER CHANNEL, which is the new half of this file.
     *
     * CC1S is cleared so channel 1 is an OUTPUT rather than a capture input,
     * OC1M is 011 for toggle, and OC1PE is cleared because there is nothing to
     * preload: the compare value is written once and never changes while the
     * timer runs.
     *
     * The mask covers OC1M's fourth bit as well as its low three. On this part
     * TIM_CCMR1_OC1M_Msk is (0x1007 << 4), so it spans bits 6 to 4 and bit 16,
     * and a mode written with only the low three bits cleared would leave bit 16
     * holding whatever it held. */
    TIM3->CCMR1 = (TIM3->CCMR1 & ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M
                                   | TIM_CCMR1_OC1PE))
                  | (3u << TIM_CCMR1_OC1M_Pos);

    /* Match at the counter's zero, where the update event is. */
    TIM3->CCR1 = 0u;

    /* The output to the pad, active high. CC1E is the whole of the enable on a
     * general-purpose timer; see the note at the top about the BDTR write that
     * must not be here. */
    TIM3->CCER = (TIM3->CCER & ~TIM_CCER_CC1P) | TIM_CCER_CC1E;

    /* READ BACK BOTH, because this is exactly the kind of configuration that
     * fails silently: with the mode left at its reset value the channel drives a
     * level and never toggles, the console reports a running timer, and the
     * witness shows a flat line that looks like a broken wire. */
    const uint32_t mode_want = (3u << TIM_CCMR1_OC1M_Pos);
    const uint32_t mode_mask = TIM_CCMR1_CC1S | TIM_CCMR1_OC1M;
    const bool mode_ok = (TIM3->CCMR1 & mode_mask) == mode_want;
    tim3_report("marker channel mode", mode_want, TIM3->CCMR1, mode_ok);

    const bool out_ok = (TIM3->CCER & TIM_CCER_CC1E) != 0u;
    tim3_report("marker channel output", TIM_CCER_CC1E, TIM3->CCER, out_ok);

    if (!mode_ok || !out_ok) {
        return ACQ_ERR_MARKER_CHANNEL;
    }

    /* AND THE PAD LAST, after the channel is configured and enabled, so the pin
     * is never handed to a peripheral that has not been told what to drive. With
     * the timer still stopped the compare has not matched, the channel's output
     * reference is at its reset level, and the pin sits low: the same state
     * marker_init() left it in by calling marker_low(), so the first edge the
     * witness sees is still a rising one.
     *
     * IT REPORTS ITS TWO REGISTERS AND REFUSES, since Monday 26 October 2026.
     * Before that this call returned nothing and printed nothing, so a build
     * whose channel was perfect and whose pad was not looked identical on the
     * console to one that worked, and the difference only appeared sixty seconds
     * later as 49.98 Hz on the witness, which is the mains. */
    if (!marker_init_tim3_ch1()) {
        return ACQ_ERR_MARKER_PAD;
    }

    return 0;
}

void tim3_start(void)
{
    TIM3->CR1 |= TIM_CR1_CEN;
    printf("  tim3 started, so conversions begin on its update event and the "
           "marker toggles with them\r\n");
}
