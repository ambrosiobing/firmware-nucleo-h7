/* A known frequency on a pin, so the frequency counter can be shown to count.
 *
 * WHY THIS EXISTS. c/instr/freqcount.c has been on the board since Monday 5
 * October 2026 and has never counted anything, because nothing has ever been
 * connected to PD12. Every run reports 0 millihertz, which is the correct
 * frequency of an idle line held high by its pull-up and is also exactly what a
 * refusal returns. So the counting path is configured and unproven, and no
 * amount of host work can change that.
 *
 * This drives TIM1 channel 3 on PE13. One wire from PE13 to PD12 and the counter
 * has something to count. Both pins reach the ST Zio header, settled Tuesday 6
 * October 2026: PE13 is Arduino D3 and PD12 is Arduino D29, from Zephyr's board
 * definition and the STM32 Arduino core's variant for this exact part, which
 * were written independently and agree on the one pin both of them cover.
 *
 * WHY THIS IS WORTH MORE THAN A SELF TEST, and the first design was a self test.
 * ST's LPTIM_PWMExternalClock example puts LPTIM1_OUT on PD13 and LPTIM1_IN1 on
 * PD12, adjacent pins, so one wire between them looks like the obvious
 * arrangement. It is a feedback loop: that example clocks the counter from IN1
 * and generates its output from the compare, so the output is derived from the
 * input, and counting a signal produced by the counter under test establishes
 * nothing.
 *
 * TIM1 is clocked from APB2 and therefore from the PLL, while the counter's gate
 * is the 32.768 kHz crystal through c/instr/lseref.c. So the two paths share no
 * component but the crystal itself: lseref counts core cycles inside the part
 * with DWT_CYCCNT, and freqcount counts edges arriving on a pin from outside the
 * counter. Two instruments, one quantity.
 *
 * AND IT CAN FAIL INFORMATIVELY, which a loop cannot. If the counted frequency
 * matches the planned one, the crystal gate and the cycle counter are confirmed
 * against each other, and the seven clock readings of Sunday 4 October 2026 to
 * Tuesday 6 October 2026 gain a second witness. If they disagree, the SIZE of
 * the disagreement says which kind of error it is: a ratio near a small integer
 * points at a prescaler or divider misread, a few hundred parts per million
 * points at the crystal, and a disagreement that moves between runs points back
 * at the debugger's 8 MHz, which is already known to move by a part in a
 * thousand.
 *
 * WHAT IS NOT IN THIS FILE. Every calculation is in c/instr/pwmmath.c, which
 * reads no register and is driven from a host against 33 hand-written cases in
 * python/tests/test_pwmmath.py. This file keeps only what the part can do:
 * enabling clocks, configuring the pin, writing the timer, and reading back what
 * it wrote. That split is the one c/clock/clocktree.c and c/instr/freqmath.c
 * already have, for the reason clocktree.h states: an arithmetic error that only
 * a flash can find is an arithmetic error nobody will find.
 *
 * THE ONE REGISTER FACT THAT WOULD OTHERWISE COST A BENCH SESSION. TIM1 is an
 * advanced-control timer, so its outputs are gated by MOE in TIM1_BDTR. With
 * that bit clear every other register can be correct and the pin stays exactly
 * where the GPIO configuration left it, with nothing in any readback to say why.
 * The general-purpose timers have no such bit, so a working TIM3 example copied
 * onto TIM1 produces silence. stm32h7a3_regs.h says this too, beside the
 * definition.
 */
#ifndef PWMSRC_H
#define PWMSRC_H

#include <stdint.h>

/* What the source was asked for, what it will actually produce, and the fields
 * that produce it. Reported rather than returned piecemeal so a caller can print
 * the whole plan, which is what makes a disagreement attributable later. */
typedef struct {
    uint32_t tim_hz;        /* the timer clock used, in hertz                */
    uint32_t want_hz;       /* what was asked for                            */
    uint16_t psc;           /* TIM1_PSC as written, already carrying its -1   */
    uint16_t arr;           /* TIM1_ARR as written, already carrying its -1   */
    uint16_t ccr;           /* TIM1_CCR3, the half period                     */
    uint64_t achieved_mhz;  /* what it produces, in millihertz                */
    int32_t  error_ppm;     /* achieved against want, positive when higher    */
} pwmsrc_t;

#define PWMSRC_OK                   0
#define PWMSRC_ERR_REGS           (-1)  /* addresses not confirmed for this part */
#define PWMSRC_ERR_PCLK2_UNKNOWN  (-2)  /* board_pclk2_hz() is 0, clock unestablished */
#define PWMSRC_ERR_APB2_PRESCALER (-3)  /* CDPPRE2 is not divide by one; see below */
#define PWMSRC_ERR_PLAN           (-4)  /* pwmmath refused the target */
#define PWMSRC_ERR_READBACK       (-5)  /* a register does not hold what was written */

/* Configure TIM1 channel 3 on PE13 for want_hz and start it.
 *
 * Returns PWMSRC_OK and fills out, or a named refusal. out is filled in as far
 * as the function got, so a PWMSRC_ERR_READBACK still carries the plan that was
 * attempted, which is the information a reader needs.
 *
 * ON PWMSRC_ERR_APB2_PRESCALER, WHICH IS A REFUSAL AND NOT A DEFECT. The timer
 * clock is not simply the APB2 clock: RCC_CFGR carries TIMPRE, and ST's own low
 * layer names its two settings TWICE and FOUR_TIMES, so the timer clock becomes
 * a multiple of the bus clock once the APB prescaler divides by anything. Where
 * exactly that multiplication starts, and where it is capped at HCLK, is in
 * RM0455, which is on neither laptop. So this refuses every CDPPRE2 but divide
 * by one, where both values of TIMPRE give the same number and the answer does
 * not depend on the half of the rule nobody has read. p01-pll280 leaves
 * RCC_CDCFGR2 at 0, which is divide by one, confirmed on the board seven times.
 */
int pwmsrc_start(uint32_t want_hz, pwmsrc_t *out);

/* Stop the output and leave the pin where it is.
 *
 * Clears CEN and MOE. The GPIO configuration is deliberately left alone: a pin
 * returned to its reset state would float, and a floating line into the counter
 * would read as an arbitrary frequency rather than as nothing. With MOE clear
 * the timer's output is disconnected and the pull-up holds the line, which is
 * the state that reads as 0 millihertz. */
int pwmsrc_stop(void);

/* A refusal as a short stable token, or "ok". The same idea as
 * clocktree_refusal_text: one error value tells a reader nothing about where to
 * look, and one word sends them to one register. */
const char *pwmsrc_refusal_text(int code);

#endif /* PWMSRC_H */
