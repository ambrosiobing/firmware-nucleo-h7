/* The signal source's arithmetic, with no register in it.
 *
 * WHAT THIS IS FOR. c/instr/freqcount.c counts edges arriving on PD12 and has
 * never been shown to count anything, because nothing has ever been connected to
 * that pin. The arrangement that would show it is TIM1 channel 3 on PE13 driven
 * at a known frequency, one wire to PD12, and the count compared against the
 * 32.768 kHz crystal gate. Both pins reach the ST Zio header, settled Tuesday 6
 * October 2026: PE13 is Arduino D3 and PD12 is Arduino D29.
 *
 * WHY IT IS A SEPARATE FILE FROM THE DRIVER, which is the third time this
 * repository has made the same split and the reason has not changed. A file that
 * dereferences TIM1 cannot be called on a host, so its arithmetic could only
 * ever be tested by flashing the board and believing the number. c/clock/
 * clocktree.c was split out on Sunday 4 October 2026 and c/instr/freqmath.c on
 * Monday 5 October 2026 for exactly this. An arithmetic error that only a flash
 * can find is an arithmetic error nobody will find.
 *
 * So this file reads no register, includes no board header, and every function
 * in it is a pure function of its arguments.
 *
 * WHAT IS AT RISK HERE, which is what the host suite drives:
 *
 *   - the prescaler choice. TIM1 is a 16-bit counter on this part, so the total
 *     divider has to be split across PSC and ARR, and the split is not unique.
 *     This picks the SMALLEST prescaler that lets ARR fit, which is the one that
 *     leaves the most resolution, and a wrong choice here still produces a
 *     plausible frequency.
 *   - the off-by-one on both fields. The hardware divides by PSC+1 and counts
 *     ARR+1 states, so a divider of 140 is PSC 0 and ARR 139, and writing 140
 *     gives a frequency 0.7 per cent low, which is exactly the size of error
 *     that looks like a measurement result.
 *   - multiply before divide with a 64-bit intermediate, the same trap
 *     freqmath.c carries, because the achieved frequency is reported in
 *     millihertz so that it can be compared with freqmath's measurement without
 *     either side rounding first.
 *   - four refusals, each at its own boundary.
 *
 * THE REFUSALS, and they return a named negative rather than a wrong plan. */
#ifndef PWMMATH_H
#define PWMMATH_H

#include <stdint.h>

#define PWMMATH_OK                0
#define PWMMATH_NO_TIMER_CLOCK  (-1)   /* a timer clock of zero divides nothing */
#define PWMMATH_NO_TARGET       (-2)   /* a target of zero has no divider       */
#define PWMMATH_TOO_FAST        (-3)   /* the hardware cannot divide by one     */
#define PWMMATH_TOO_SLOW        (-4)   /* the prescaler would exceed 16 bits    */

/* A plan the driver can write into the registers without arithmetic of its own.
 *
 * psc and arr go into TIM1_PSC and TIM1_ARR exactly as they are, already
 * carrying their minus one. ccr goes into TIM1_CCR3 and is the half period, so
 * the duty cycle is as close to 50 per cent as an integer compare allows; the
 * counter under test triggers on an edge, so the duty does not change the
 * frequency, and 50 per cent is chosen only so that a logic probe or an
 * oscilloscope sees something sensible if one is ever attached.
 *
 * achieved_mhz is the frequency the plan actually produces, in millihertz, which
 * is almost never the frequency that was asked for. */
struct pwmmath_plan {
    uint16_t psc;
    uint16_t arr;
    uint16_t ccr;
    uint64_t achieved_mhz;
};

/* Choose PSC and ARR for want_hz from a timer clock of tim_hz.
 *
 * Returns PWMMATH_OK and fills out, or one of the refusals above and leaves out
 * untouched. */
int pwmmath_plan(uint32_t tim_hz, uint32_t want_hz, struct pwmmath_plan *out);

/* The frequency a given PSC and ARR produce, in millihertz. Multiply before
 * divide, 64-bit intermediate. Returns 0 for a timer clock of zero. */
uint64_t pwmmath_achieved_mhz(uint32_t tim_hz, uint16_t psc, uint16_t arr);

/* The 50 per cent compare value for a given ARR, which is (arr + 1) / 2. */
uint16_t pwmmath_ccr_half(uint16_t arr);

/* How far the achieved frequency is from the one asked for, in parts per
 * million, positive when the achieved frequency is the higher. Halves away from
 * zero, saturating at the int32_t limits, the same convention clocktree_bias
 * uses. Returns 0 when want_hz is 0, because there is no error to express. */
int32_t pwmmath_error_ppm(uint64_t achieved_mhz, uint32_t want_hz);

/* The timer kernel clock, and this one refuses far more than it answers.
 *
 * On this part the timer clock is not simply the APB clock. RCC_CFGR carries
 * TIMPRE, and ST's own low layer names its two settings LL_RCC_TIM_PRESCALER_
 * TWICE and LL_RCC_TIM_PRESCALER_FOUR_TIMES, so the timer clock is two or four
 * times the APB clock once the APB prescaler divides by anything. Where exactly
 * that multiplication starts and where it is capped at HCLK is in RM0455, which
 * is not on either laptop.
 *
 * SO ONLY ONE CASE IS ANSWERED, and it is the case this board is in. When the
 * APB prescaler divides by one, the APB clock equals HCLK, and the timer clock
 * equals both of them under either value of TIMPRE. That makes the answer
 * independent of the part of the rule that has not been read. p01-pll280 leaves
 * RCC_CDCFGR2 at 0, which is divide by one, confirmed on the board five times.
 *
 * apb_prescaler is the DIVISOR, not the register field: 1, 2, 4, 8 or 16.
 * Returns the timer clock in hertz, or 0 to refuse, which is every case where
 * the divisor is not 1. A refusal here is not a defect, it is this file
 * declining to invent the half of the rule nobody has read. */
uint32_t pwmmath_timer_hz(uint32_t pclk_hz, uint32_t apb_prescaler);

#endif /* PWMMATH_H */
