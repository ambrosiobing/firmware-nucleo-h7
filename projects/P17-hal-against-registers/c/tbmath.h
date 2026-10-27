/* tbmath.h: chapter 17's timebase arithmetic, the half that needs no peripheral.
 *
 * THE NAME follows c/instr/freqmath.c, pwmmath.c and adcmath.c: the arithmetic
 * split out of a file that touches registers, so it can be tested on a host. It
 * is deliberately not timebase.h, after a header called features.h shadowed
 * glibc's own on Tuesday 27 October 2026 and failed the build three files away.
 *
 * WHAT THIS IS, AND WHICH CRITERIA IT SERVES. Chapter 17 compares four
 * implementations of one bus read, and that comparison needs the peripheral and
 * the shield. Three of its acceptance criteria do not:
 *
 *   "The instrument's own cost is measured and subtracted, and the subtraction
 *    is visible in the source rather than folded into a constant."
 *   "The first run and the median of the remainder are reported separately."
 *   "The timebase substitution is demonstrated on all three peripherals, with
 *    the wrap period of each stated."
 *
 * Those are arithmetic on counter values, and this is that arithmetic. Criterion
 * 1, the byte-identical harness over four bus reads, is bench work and is not
 * here.
 *
 * THE CHAPTER'S THREE WRAP PERIODS ARE CORRECT, which is worth saying because
 * most of what this volume finds by checking is wrong. Step 8 states about
 * 15.3 seconds for a 32-bit counter at 280 MHz, about 71.6 minutes for a 32-bit
 * counter at 1 MHz, and about 60 milliseconds for a 24-bit counter at the core
 * clock. They are 15.337 s, 71.583 min and 59.92 ms, and the test recomputes
 * them rather than quoting them.
 *
 * FINDING ONE: THE TIMEBASE INTERFACE HAS NO FIELD FOR THE ONE THING THAT
 * DIFFERS. Step 8's struct carries a start function, a tick reader, a frequency
 * and a width in bits, and the comment beside its third instance says "counts
 * down". SysTick does count down. There is no field for the direction, so an
 * elapsed interval computed through that abstraction is wrong for one of the
 * three peripherals it exists to make interchangeable, and wrong in the
 * direction that looks plausible: a short interval reads as nearly a whole wrap.
 * tb_elapsed() therefore takes the direction as an argument. This is the
 * chapter's own subject turned on the chapter: an abstraction hiding the thing
 * that matters.
 *
 * FINDING TWO: THE INSTRUMENT SUBTRACTION UNDERFLOWS AND REPORTS 4.29 BILLION
 * CYCLES. Step 6 calibrates the cost of reading the counter as the minimum over
 * 64 back-to-back reads and then returns (b - a) - cyc_overhead on a uint32_t.
 * When a measured interval is below that minimum, which a minimum over 64 trials
 * does not prevent, the subtraction wraps to a number near 2^32, and at 280 MHz
 * that is reported as just over 15 seconds for an operation that took almost no
 * time. An unsigned subtraction that can produce the largest number in the type
 * is not a measurement. tb_subtract_overhead() refuses instead, and the refusal
 * carries the two inputs so the calibration can be looked at.
 *
 * WHAT IS NOT HERE. The four implementations, the bus, the sensor, the code
 * sizes, the cycle counts, the ratio of overhead to work and the Rust variant's
 * peripheral module routing. There is no measured cycle in this project: every
 * number it has seen was synthetic.
 */
#ifndef TBMATH_H
#define TBMATH_H

#include <stdint.h>

/* Enough samples for the chapter's calibration loop of 64 and its repetition
 * counts, with room for the first run to be held out. */
#define TB_MAX_SAMPLES 256u

/* The three shapes step 8 names. The direction is a field here because the
 * chapter's own struct does not have one. */
#define TB_COUNTS_UP 0
#define TB_COUNTS_DOWN 1

typedef struct {
    uint32_t first;       /* the first run, reported separately on purpose */
    uint32_t median;      /* of the remainder, which is what step 6 asks   */
    uint32_t smallest;
    uint32_t largest;
    uint32_t count;       /* how many went into the median                 */
} tb_report_t;

#define TB_ERR_ARGUMENT  (-1) /* a null pointer, or no samples             */
#define TB_ERR_WIDTH     (-2) /* a counter width outside 1 to 32 bits      */
#define TB_ERR_DIRECTION (-3) /* a direction that is neither up nor down   */
#define TB_ERR_RANGE     (-4) /* a count that does not fit the stated width */
#define TB_ERR_RATE      (-5) /* a tick frequency at or below zero         */
#define TB_ERR_UNDERFLOW (-6) /* an interval below the instrument's own cost */
#define TB_ERR_SAMPLES   (-7) /* fewer samples than a first run and a median */

/* Ticks between two readings of one counter, correct across a wrap and in
 * either direction. width_bits is where the counter wraps: 32 for the cycle
 * counter and a general timer, 24 for SysTick. */
int tb_elapsed(uint32_t start, uint32_t end, uint32_t width_bits,
               int counts_down, uint32_t *out);

/* The wrap period in nanoseconds, from the tick frequency and the width. In
 * nanoseconds because the three answers span 60 milliseconds to 72 minutes and
 * an integer of nanoseconds holds all of them inside 64 bits. */
int tb_wrap_ns(double hz, uint32_t width_bits, uint64_t *out);

/* A raw interval less the instrument's own cost, refusing rather than wrapping
 * when the interval is below it. */
int tb_subtract_overhead(uint32_t raw, uint32_t overhead, uint32_t *out);

/* The first run and the median of the remainder, which step 6 asks to be kept
 * apart because the difference between them is a cache effect rather than
 * noise. Refuses fewer than two samples, since one sample has no remainder. */
int tb_report(const uint32_t *samples, uint32_t n, tb_report_t *out);

#endif /* TBMATH_H */
