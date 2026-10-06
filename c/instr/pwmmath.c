/* The signal source's arithmetic. See pwmmath.h for what is at risk here.
 *
 * NOTHING IN THIS FILE READS A REGISTER, and that is the point of it existing
 * separately from the driver. If an include of a board header ever appears here,
 * the host suite stops being able to drive it and the arithmetic goes back to
 * being untestable. */
#include "pwmmath.h"

#define PSC_MAX   65535u
#define ARR_MAX   65535u
#define STATES_MAX 65536u   /* ARR_MAX + 1, the most states a 16-bit counter has */


/* Halves away from zero, saturating, which is the same helper clocktree.c uses
 * and is written out again here rather than shared, because sharing it would
 * make this file depend on the clock tree for a rounding rule. */
static int32_t ppm(int64_t delta, int64_t den)
{
    if (den == 0) {
        return 0;
    }
    {
        const int64_t half   = den / 2;
        const int64_t scaled = delta * 1000000;
        const int64_t q = (delta >= 0) ? ((scaled + half) / den) : ((scaled - half) / den);
        if (q > (int64_t) INT32_MAX) { return INT32_MAX; }
        if (q < (int64_t) INT32_MIN) { return INT32_MIN; }
        return (int32_t) q;
    }
}


uint16_t pwmmath_ccr_half(uint16_t arr)
{
    /* (arr + 1) / 2 in 32-bit arithmetic, because arr + 1 overflows a uint16_t
     * at arr == 65535 and that is the value a refused-too-slow plan clamps to.
     * The result fits a uint16_t for every arr: the largest is 32768, which is
     * (65535 + 1) / 2. */
    return (uint16_t) (((uint32_t) arr + 1u) / 2u);
}


uint64_t pwmmath_achieved_mhz(uint32_t tim_hz, uint16_t psc, uint16_t arr)
{
    if (tim_hz == 0u) {
        return 0u;
    }
    {
        const uint64_t divider = ((uint64_t) psc + 1u) * ((uint64_t) arr + 1u);
        /* Multiply before divide. The 1000 is what makes the result millihertz,
         * and doing it after the division would throw away exactly the digits
         * the comparison against freqmath needs. tim_hz is at most 280000000 on
         * this part, so the product is at most 2.8e11 and nowhere near the
         * 64-bit limit. */
        return ((uint64_t) tim_hz * 1000u) / divider;
    }
}


int32_t pwmmath_error_ppm(uint64_t achieved_mhz, uint32_t want_hz)
{
    if (want_hz == 0u) {
        return 0;
    }
    {
        const int64_t want_mhz = (int64_t) want_hz * 1000;
        return ppm((int64_t) achieved_mhz - want_mhz, want_mhz);
    }
}


uint32_t pwmmath_timer_hz(uint32_t pclk_hz, uint32_t apb_prescaler)
{
    /* Only the divide-by-one case is answered, and pwmmath.h says why at
     * length: it is the one case where both values of TIMPRE give the same
     * number, so the answer does not depend on the half of the rule that is in
     * RM0455 and has not been read. */
    if (apb_prescaler != 1u) {
        return 0u;
    }
    return pclk_hz;
}


int pwmmath_plan(uint32_t tim_hz, uint32_t want_hz, struct pwmmath_plan *out)
{
    if (out == 0) {
        return PWMMATH_NO_TARGET;
    }
    if (tim_hz == 0u) {
        return PWMMATH_NO_TIMER_CLOCK;
    }
    if (want_hz == 0u) {
        return PWMMATH_NO_TARGET;
    }

    {
        /* The total divider, to nearest. The hardware can only deliver an
         * integer, so this is already the best any split can do. */
        const uint64_t total = (((uint64_t) tim_hz) + ((uint64_t) want_hz / 2u))
                               / (uint64_t) want_hz;

        if (total < 2u) {
            /* The smallest divider a PWM output can have is 2: one state high
             * and one low. A target above half the timer clock is therefore not
             * a rounding problem, it is outside the hardware. */
            return PWMMATH_TOO_FAST;
        }

        {
            /* The smallest prescaler that leaves the counter able to reach the
             * rest of the divider, which is ceil(total / 65536) expressed
             * without floating point. This is the choice pwmmath.h calls out as
             * at risk: a larger prescaler would also work and would throw away
             * resolution for nothing. */
            const uint64_t divisor = (total + (uint64_t) STATES_MAX - 1u)
                                     / (uint64_t) STATES_MAX;

            if (divisor > (uint64_t) STATES_MAX) {
                /* Even a full prescaler cannot bring the count inside 16 bits,
                 * so the target is too low for this timer at this clock. */
                return PWMMATH_TOO_SLOW;
            }

            {
                const uint64_t step   = divisor * (uint64_t) want_hz;
                /* The count, to nearest, after the prescaler has taken its
                 * share. Rounding here rather than truncating is what keeps the
                 * achieved frequency centred on the target instead of always
                 * landing above it. */
                uint64_t states = (((uint64_t) tim_hz) + (step / 2u)) / step;

                if (states < 2u) {
                    states = 2u;
                }
                if (states > (uint64_t) STATES_MAX) {
                    states = (uint64_t) STATES_MAX;
                }

                out->psc = (uint16_t) (divisor - 1u);
                out->arr = (uint16_t) (states - 1u);
                out->ccr = pwmmath_ccr_half(out->arr);
                out->achieved_mhz = pwmmath_achieved_mhz(tim_hz, out->psc, out->arr);
                return PWMMATH_OK;
            }
        }
    }
}
