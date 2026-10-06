/* c/instr/adcmath.c: the converter's arithmetic. adcmath.h carries the design,
 * the four part-identity decisions and the citation for every rule here.
 *
 * ST'S CONDITIONALS ARE WRITTEN LITERALLY, so that the code matches the lines it
 * cites, and python/tests/test_adcmath.py checks each against a second form
 * computed a different way. That is the arrangement c/instr/pwmmath.c uses for
 * the TIMPRE rule and it caught nothing there, which is the point: a check that
 * only fires when something is wrong looks like waste until it fires.
 */
#include "adcmath.h"

/* ST halves the clock before comparing, at stm32h7xx_hal_adc.c line 3992. The
 * thresholds below are its lines 3993, 3997 and 4001 in order. */
#define ADCMATH_BOOST_T0  6250000u
#define ADCMATH_BOOST_T1 12500000u
#define ADCMATH_BOOST_T2 25000000u

/* This file's own ceiling and not ST's. ST's function has no upper branch: above
 * 25 MHz of halved clock it selects 3 and asks no questions. A caller arriving
 * here with more than 100 MHz has an arithmetic error upstream, most likely a
 * clock tree it misread, and a confident BOOST of 3 would carry that error into
 * a converter that then samples wrongly rather than refusing. */
#define ADCMATH_CLOCK_MAX_HZ 100000000u

bool adcmath_boost(uint32_t adc_clk_hz, uint32_t *boost)
{
    uint32_t freq;

    if (boost == 0) {
        return false;
    }
    if (adc_clk_hz == 0u || adc_clk_hz > ADCMATH_CLOCK_MAX_HZ) {
        return false;
    }

    /* The halving is ST's and comes first. Comparing the unhalved clock against
     * these thresholds would select one setting too high at every boundary,
     * which is the kind of error that works at one clock and not at the next. */
    freq = adc_clk_hz / 2u;

    if (freq <= ADCMATH_BOOST_T0) {
        *boost = 0u;
    } else if (freq <= ADCMATH_BOOST_T1) {
        *boost = 1u;
    } else if (freq <= ADCMATH_BOOST_T2) {
        *boost = 2u;
    } else {
        *boost = 3u;
    }
    return true;
}

/* The divisors the two fields can express, kept as a list rather than a range
 * because the asynchronous set is not contiguous: the even values 2 to 12, then
 * 16, 32, 64, 128 and 256. A range check would accept 14 and 24, which PRESC
 * cannot encode, and the resulting clock would be a number nothing produces. */
static bool divisor_is_expressible(uint32_t divisor)
{
    switch (divisor) {
    /* CKMODE, synchronous: HCLK divided by 1, 2 or 4 */
    case 1u:
    /* PRESC, asynchronous: the even values and then the powers of two */
    case 2u: case 4u: case 6u: case 8u: case 10u: case 12u:
    case 16u: case 32u: case 64u: case 128u: case 256u:
        return true;
    default:
        return false;
    }
}

bool adcmath_clock_hz(uint32_t source_hz, uint32_t divisor, uint32_t *adc_clk_hz)
{
    if (adc_clk_hz == 0) {
        return false;
    }
    if (source_hz == 0u || !divisor_is_expressible(divisor)) {
        return false;
    }
    *adc_clk_hz = source_hz / divisor;
    return true;
}

/* This part's highest external channel number. It is the one figure in this file
 * that wants a datasheet rather than ST's pack, and adcmath.h says so: 19 is the
 * conservative choice until that is read. Too low refuses a channel that exists,
 * which is visible; too high accepts one that does not, which is not. */
#define ADCMATH_CHANNEL_MAX 19u

bool adcmath_pcsel_bit(uint32_t channel, uint32_t *bit)
{
    if (bit == 0) {
        return false;
    }
    /* ST's expression masks with 0x1F, which this deliberately does not rely on:
     * masking 36 gives 4, and a converter preselecting channel 4 when asked for
     * 36 is wrong in a way no reading of the register would reveal. */
    if (channel > ADCMATH_CHANNEL_MAX) {
        return false;
    }
    *bit = 1u << channel;
    return true;
}
