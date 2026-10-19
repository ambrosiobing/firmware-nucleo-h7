/* tim6_bringup.h: the sampling timer, shared by the back ends that need one.
 *
 * WHY THIS EXISTS, and it is the same reason adc_bringup.h exists. Two of P06's
 * three back ends need TIM6 running at the sampling rate and emitting a trigger
 * output on its update event: the timer back end, which has had it inline since
 * Wednesday 7 October 2026, and the transfer-engine back end, which could not be
 * written until this decision was taken. The tick back end needs no timer at all,
 * because its trigger is software and its cadence is SysTick.
 *
 * THE DECISION, taken by Joseph on Monday 19 October 2026 and recorded because the
 * alternatives were real. Three ways were on the table: duplicate the setup in the
 * second back end with the duplication named in a comment, as option A did for
 * apb1_divisor() on Friday 9 October 2026; factor it here; or leave the second back
 * end refusing until a wider decision. The factoring was chosen on the ground that
 * **a known duplication is not a reason to leave code unwritten**, and that naming a
 * duplication in a comment still leaves two copies that can drift while nothing
 * notices. One shared function is the smallest change that removes the block.
 *
 * WHAT IS SHARED AND WHAT IS NOT, which is the whole of the interface.
 *
 * tim6_bring_up() clocks the timer, derives its kernel frequency rather than
 * assuming it, plans the prescaler and reload for the requested rate, writes them,
 * and selects the trigger output. It leaves the timer CONFIGURED AND STOPPED. Every
 * refusal it can make is one the caller returns unchanged, so the error numbering in
 * acq_errors.h keeps its meaning across both back ends.
 *
 * tim6_start() is separate, and the separation is the point rather than tidiness.
 * The timer must not run until the converter is armed: a trigger arriving at an
 * unarmed converter does nothing, so starting first would lose samples whose absence
 * nothing would report. Both back ends therefore call bring_up, then the converter's
 * own bring-up, then start, in that order.
 *
 * WHAT IT DOES NOT DO. It does not enable an interrupt, because neither back end
 * wants one from the timer: the timer's output is consumed by the converter in
 * hardware and the handler that matters belongs to whatever moves the samples. It
 * does not touch TIMPRE, which it reads. And it does not choose the rate, which the
 * caller passes, so a back end sampling at something other than ACQ_RATE_HZ needs no
 * change here.
 *
 * THE MOVE'S CRITERION, published before the move as the converter bring-up's was:
 * the timer build's console output must come back BYTE-IDENTICAL, line for line,
 * because a pure move changes no register write and no format string. The two
 * measured poll counts in that output belong to the converter and are free to move;
 * nothing this file prints is a measurement.
 */
#ifndef TIM6_BRINGUP_H
#define TIM6_BRINGUP_H

#include <stdint.h>

/* Configure TIM6 at rate_hz with its trigger output on the update event, and
 * leave it stopped. Returns 0, or ACQ_ERR_PCLK1_UNKNOWN when the clock tree was
 * never decoded, or ACQ_ERR_APB1_PRESCALER when the bus prescaler holds a ratio
 * this volume has not sourced. Prints one line naming the timer clock it derived
 * and the three inputs it derived it from. */
int tim6_bring_up(uint32_t rate_hz);

/* Start the timer. Call this AFTER the converter is armed, never before. Prints
 * one line, because the start is the irreversible step in the sequence and a
 * reader of the console needs to see where it happened. */
void tim6_start(void);

#endif /* TIM6_BRINGUP_H */
