/* tbmath.c: elapsed ticks, wrap periods, the instrument subtraction, the report.
 *
 * The header says at length why the direction is an argument and why the
 * subtraction refuses.
 */
#include "tbmath.h"

#include <stddef.h>

static uint32_t mask_of(uint32_t width_bits)
{
    if (width_bits >= 32u) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t) ((1u << width_bits) - 1u);
}

int tb_elapsed(uint32_t start, uint32_t end, uint32_t width_bits,
               int counts_down, uint32_t *out)
{
    if (out == NULL) {
        return TB_ERR_ARGUMENT;
    }
    if (width_bits == 0u || width_bits > 32u) {
        return TB_ERR_WIDTH;
    }
    if (counts_down != TB_COUNTS_UP && counts_down != TB_COUNTS_DOWN) {
        return TB_ERR_DIRECTION;
    }

    const uint32_t mask = mask_of(width_bits);
    /* A reading that does not fit the stated width is a reading from a
     * different counter, or a width that is wrong. Either way the elapsed
     * interval computed from it would be arithmetic on a mistake. */
    if ((start & mask) != start || (end & mask) != end) {
        return TB_ERR_RANGE;
    }

    /* The subtraction is masked to the counter's own width, so one wrap is
     * handled and two are indistinguishable from none. That bound is the wrap
     * period, which is why tb_wrap_ns exists and why step 8 reports it. */
    const uint32_t difference = counts_down
        ? ((start - end) & mask)   /* a down counter falls as time passes */
        : ((end - start) & mask);
    *out = difference;
    return 0;
}

int tb_wrap_ns(double hz, uint32_t width_bits, uint64_t *out)
{
    if (out == NULL) {
        return TB_ERR_ARGUMENT;
    }
    if (width_bits == 0u || width_bits > 32u) {
        return TB_ERR_WIDTH;
    }
    if (!(hz > 0.0)) {
        return TB_ERR_RATE;
    }
    /* The count of ticks in one wrap is two to the width, which is one more
     * than the mask and does not fit 32 bits at width 32. */
    const uint64_t ticks = (uint64_t) 1 << width_bits;
    /* Nanoseconds, rounded to nearest, from a tick count and a frequency. The
     * division is done in floating point because the frequency is, and the
     * result is an integer of nanoseconds so that two implementations can
     * compare it exactly. */
    const double ns = ((double) ticks) * 1.0e9 / hz;
    *out = (uint64_t) (ns + 0.5);
    return 0;
}

int tb_subtract_overhead(uint32_t raw, uint32_t overhead, uint32_t *out)
{
    if (out == NULL) {
        return TB_ERR_ARGUMENT;
    }
    if (raw < overhead) {
        /* Step 6 writes this as (b - a) - cyc_overhead on a uint32_t, where it
         * wraps to a number near two to the thirty-second: at 280 MHz that is
         * reported as fifteen seconds for an operation that took almost none.
         * A subtraction that can produce the largest number in the type is not
         * a measurement, so this is a refusal and the caller is expected to
         * look at the calibration rather than at the result. */
        return TB_ERR_UNDERFLOW;
    }
    *out = raw - overhead;
    return 0;
}

static void sort_small(uint32_t *v, uint32_t count)
{
    /* An insertion sort over at most TB_MAX_SAMPLES values, allocating
     * nothing, which is the same choice P07's report and P12's gate made. */
    for (uint32_t i = 1u; i < count; i++) {
        const uint32_t key = v[i];
        uint32_t j = i;
        while (j > 0u && v[j - 1u] > key) {
            v[j] = v[j - 1u];
            j--;
        }
        v[j] = key;
    }
}

int tb_report(const uint32_t *samples, uint32_t n, tb_report_t *out)
{
    if (samples == NULL || out == NULL) {
        return TB_ERR_ARGUMENT;
    }
    if (n > TB_MAX_SAMPLES) {
        return TB_ERR_SAMPLES;
    }
    if (n < 2u) {
        /* One sample has no remainder to take a median of, and step 6 asks for
         * the first run and the median of the rest as two numbers. */
        return TB_ERR_SAMPLES;
    }

    uint32_t rest[TB_MAX_SAMPLES];
    const uint32_t count = n - 1u;
    for (uint32_t i = 0u; i < count; i++) {
        rest[i] = samples[i + 1u];
    }
    sort_small(rest, count);

    out->first = samples[0];
    out->smallest = rest[0];
    out->largest = rest[count - 1u];
    out->count = count;
    if ((count & 1u) != 0u) {
        out->median = rest[count / 2u];
    } else {
        /* The mean of the two middle values, which is what a reader comparing
         * this against a library's median will expect. Both are unsigned and
         * the sum is taken in 64 bits so the halving cannot overflow. */
        const uint64_t sum = (uint64_t) rest[count / 2u - 1u]
                           + (uint64_t) rest[count / 2u];
        out->median = (uint32_t) (sum / 2u);
    }
    return 0;
}
