/* rxspan.c: chapter 4's span split, written so a host can prove it.
 *
 * Every intermediate is unsigned 32 bit and every store into a 16 bit field is
 * an explicit cast, because the host build compiles this with -Wconversion and
 * -Wsign-conversion as errors. The arithmetic is the chapter's, unchanged: the
 * difference of two positions, masked by the size, is the advance, and the split
 * is three cases of which two produce one span and one produces two.
 */
#include "rxspan.h"

#include <stddef.h>

int rxspan_next(uint16_t size, uint16_t old_pos, uint16_t pos,
                rxspan_t out[RXSPAN_MAX], rxspan_stats_t *stats)
{
    if (out == NULL || stats == NULL) {
        return RXSPAN_ERR_ARGUMENT;
    }

    const uint32_t n = (uint32_t) size;
    if (n < RXSPAN_MIN_SIZE || (n & (n - 1u)) != 0u) {
        return RXSPAN_ERR_SIZE;
    }
    if ((uint32_t) old_pos >= n || (uint32_t) pos >= n) {
        return RXSPAN_ERR_POSITION;
    }

    /* THE ADVANCE IS MASKED AND NOT SUBTRACTED, which is the one line of this
     * file that needs the size to be a power of two. Masking gives the distance
     * travelled forward whether or not the position wrapped on the way. */
    const uint32_t advance = ((uint32_t) pos - (uint32_t) old_pos) & (n - 1u);

    stats->calls++;
    if (advance > (uint32_t) stats->peak_advance) {
        stats->peak_advance = (uint16_t) advance;
    }
    if (advance > (n / 2u)) {
        stats->near_lap++;
    }

    /* Nothing new, which is the common case and is also what a full lap looks
     * like. The header says why that cannot be distinguished here. */
    if (pos == old_pos) {
        return 0;
    }

    int count;
    if (pos > old_pos) {
        /* One span, forward, no wrap. */
        out[0].off = old_pos;
        out[0].len = (uint16_t)((uint32_t) pos - (uint32_t) old_pos);
        count = 1;
    } else {
        /* Wrapped: the tail of the buffer first, then its head. The second span
         * exists only when the position is past zero, and leaving it out when
         * it is zero is what keeps a zero length span out of the consumer's
         * hands. */
        out[0].off = old_pos;
        out[0].len = (uint16_t)(n - (uint32_t) old_pos);
        count = 1;
        if (pos > 0u) {
            out[1].off = 0u;
            out[1].len = pos;
            count = 2;
        }
    }

    stats->deliveries += (uint32_t) count;
    return count;
}
