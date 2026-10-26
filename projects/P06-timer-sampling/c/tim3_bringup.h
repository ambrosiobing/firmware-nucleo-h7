/* tim3_bringup.h: the sampling timer, which now also marks the pin.
 *
 * WHY THIS FILE REPLACES tim6_bringup.h, and the decision is Joseph's, taken on
 * Tuesday 20 October 2026 with the reasoning attached: the marker must be driven
 * from a timer channel, because that is the only arrangement that measures what
 * the three-builds table claims to compare. Do not publish a comparison the
 * instrument cannot perform.
 *
 * TIM6 COULD NOT DO IT, and that is a read fact rather than a preference. RM0455
 * Rev 13 section 46.2 page 1754 says TIM6 and TIM7 are basic timers with no
 * capture or compare channels and no pin at all. A basic timer can start a
 * conversion and can never mark it. Keeping TIM6 for the trigger and adding a
 * second timer for the pin would put two timers behind one cadence and leave a
 * reader asking which of them the measured interval belongs to.
 *
 * TIM3 DOES BOTH, and both halves are sourced:
 *
 *   the trigger   ST's LL header for this family lists TIM3_TRGO in the ADC's
 *                 regular-trigger table, so EXTSEL can select it. The value is
 *                 in adc_bringup.h with the line it was read from.
 *
 *   the pin       DS13195 Rev 1 Table 9 page 70 gives PB4 alternate function 2
 *                 as TIM3_CH1, and PB4 is the marker pin this project has used
 *                 since Tuesday 6 October 2026. The wire does not move: it stays
 *                 on CN12 pin 27, where it was put on Tuesday 20 October 2026
 *                 because the Zio position CN7 pin 19 is under the stacked
 *                 shield. See docs/the-board-and-the-wiring.md section 4.4.
 *
 * So one timer carries the cadence and the evidence of the cadence, and the
 * offset between them is a fixed number of timer ticks rather than a software
 * delay that varies with whatever the processor was doing.
 *
 * WHAT CAME WITH THIS FILE FROM tim6_bringup.h, because the decisions recorded
 * there were about the factoring and not about the timer, and they still hold.
 *
 * THE FACTORING, decided by Joseph on Monday 19 October 2026 over two real
 * alternatives: duplicate the setup in the second back end with the duplication
 * named in a comment, as option A did for apb1_divisor() on Friday 9 October
 * 2026, or leave that back end refusing until a wider decision. Factoring won on
 * the ground that a known duplication is not a reason to leave code unwritten,
 * and that naming a duplication in a comment still leaves two copies that can
 * drift while nothing notices.
 *
 * THAT MOVE'S CRITERION, published before the move and MET on Monday 19 October
 * 2026: every one of the twenty-four configuration read-back lines and the timer
 * clock line above them came back identical on the board, hex for hex, against
 * the capture committed from Friday 16 October 2026, because a pure move changes
 * no register write and no format string. The criterion's first wording was
 * wrong and was corrected before the capture rather than after it: it said the
 * console OUTPUT must come back byte-identical, which is too strong, because the
 * banner prints the image size and the image grew by 56 bytes when the moved
 * code stopped being inlined.
 *
 * WHAT TIM6 LEFT BEHIND, stated because deleting a file that ran on the board
 * needs a reason. After this change nothing calls tim6_bring_up or tim6_start,
 * and a file linked into three targets and called by none is dead code this
 * volume does not keep. Every measurement up to and including Tuesday 20 October
 * 2026 was taken with the TIM6 trigger: the twelve console captures in console/,
 * the 60 185 edges of the timer build and the 60 172 of the tick build. Those
 * remain reproducible from the commit that holds them, which is what a history
 * is for, and the images are identified in the project README by SHA-256. A file
 * retained uncalled would not make them more reproducible; it would only make
 * the tree claim a mechanism no build uses.
 *
 * THE PREDICTIONS THIS BUILD IS JUDGED BY, written here before it has run, which
 * is the only time they can be written.
 *
 * ONE. The rate must not move. The timer clock, its derivation and the
 * prescaler plan are unchanged, so the cadence is the same cadence from the same
 * oscillator. Three sessions have measured this part between about +2900 and
 * +3100 parts per million. A rate outside that band means the trigger change did
 * something this file does not describe, and the first thing to suspect is the
 * prescaler plan rather than the oscillator.
 *
 * TWO. The spread is predicted NOT to improve visibly, and saying so now is what
 * stops a floor reading from being published as a success. The software marker
 * measured 3.59 and 3.66 microseconds of spread against a witness whose own
 * floor is about 3.5 microseconds at a 10 microsecond sample period. The
 * hardware marker removes interrupt latency from the marker, and the honest
 * expectation is that the result lands on the instrument's floor and the
 * improvement stays unresolved. What the change buys is not a smaller number: it
 * is that the number now describes the sampling instants rather than the
 * handler's entries.
 *
 * THREE. The timer and transfer-engine builds become INDISTINGUISHABLE ON THE
 * MARKER, by construction rather than by measurement, and that is a consequence
 * to state rather than a result to discover. Both drive the same channel of the
 * same timer, so their marker signals are identical and no witness can separate
 * them. What still separates them is the counters: blocks delivered, application
 * overruns and converter overruns, which is where the difference between a
 * per-sample interrupt and a double-buffer transfer actually lives. A reader who
 * expects the marker to rank those two builds is reading the wrong instrument.
 *
 * FOUR. The tick build keeps its software toggle and that is not an oversight.
 * Its whole subject is that the processor is in the path of the timing, so a
 * marker that leaves the processor out would measure a cadence that build does
 * not have.
 *
 * WHAT IT DOES NOT DO. It enables no interrupt: the timer's trigger output is
 * consumed by the converter in hardware, its channel drives the pin in hardware,
 * and the handler that matters belongs to whatever moves the samples. It does not
 * touch TIMPRE, which it reads. And it does not choose the rate, which the caller
 * passes.
 */
#ifndef TIM3_BRINGUP_H
#define TIM3_BRINGUP_H

#include <stdint.h>

/* Configure TIM3 at rate_hz with its trigger output on the update event, put
 * channel 1 into toggle mode so the marker pin carries one edge per sampling
 * instant, hand the pin to alternate function 2, and leave the timer stopped.
 *
 * THE PIN IS CONFIGURED HERE, BY CALLING INTO marker.c, and the coupling is
 * deliberate. The channel and the pad are one mechanism: a channel configured
 * with the pad left as a plain output reads back perfectly and marks nothing,
 * and a pad handed to the alternate function with the channel disabled sits at
 * whatever the peripheral leaves. That is this volume's most-recorded failure
 * shape, so the two are not separable by a caller who forgets one. marker.c
 * still owns which pin it is and the citation that settled it.
 *
 * Returns 0, or ACQ_ERR_PCLK1_UNKNOWN when the clock tree was never decoded, or
 * ACQ_ERR_APB1_PRESCALER when the bus prescaler holds a ratio this volume has
 * not sourced, or ACQ_ERR_MARKER_CHANNEL when the channel's own registers did
 * not take, or ACQ_ERR_MARKER_PAD when the pin's mode or alternate function
 * number did not. Prints one line naming the timer clock it derived and the
 * three inputs it derived it from, then one line per channel register and one
 * per pad register, each with its read-back.
 *
 * THE TWO PAD LINES WERE ADDED ON Monday 26 October 2026 AND THE FIRST WITNESS
 * RUN IS WHY. It read 49.98 Hz with 583 millivolts of swing, which is the mains
 * on an open input, while the console reported the timer running, both channel
 * registers reading back, the trigger as TIM3's and 59 520 samples delivered.
 * **Every other configuration register in this build is read back and reported
 * and those two were the exception**, so the one link in the chain that leaves
 * the die was the one nothing checked. */
int tim3_bring_up(uint32_t rate_hz);

/* Start the timer. Call this AFTER the converter is armed, never before: a
 * trigger arriving at an unarmed converter does nothing, so starting first would
 * lose samples whose absence nothing would report. Prints one line, because the
 * start is the irreversible step in the sequence and a reader of the console
 * needs to see where it happened.
 *
 * The start now also puts the first edge on the marker pin, so the console line
 * and the witness's first edge are the same event. */
void tim3_start(void);

#endif /* TIM3_BRINGUP_H */
