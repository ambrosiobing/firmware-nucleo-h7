/* mpualign.c: the range rounding, criterion 4's two halves and criterion 5's
 * region encoding.
 *
 * All of it is unsigned integer arithmetic on 32-bit addresses. The twin in
 * ../python/mpualign.py has no fixed width at all, which is what makes the
 * wrap in finding four worth comparing rather than asserting.
 */
#include "mpualign.h"

#include <stddef.h>

static int is_power_of_two(uint32_t v)
{
    return (v != 0u) && ((v & (v - 1u)) == 0u);
}

int mpu_maint_range(uint32_t addr, uint32_t len, uint32_t line,
                    mpu_range_t *out)
{
    if (out == NULL) {
        return MPU_ERR_ARGUMENT;
    }
    /* A power of two, because the rounding below is a mask and not a division,
     * and because no cache has a line that is not one. */
    if (!is_power_of_two(line)) {
        return MPU_ERR_LINE;
    }
    if (len == 0u) {
        return MPU_ERR_LENGTH;
    }
    /* Finding four, checked before anything is rounded. The sum is computed in
     * 64 bits so that the test itself cannot wrap: a 32-bit check written as
     * addr + len < addr is correct but says nothing about the ROUNDED end,
     * which can cross the boundary when the unrounded sum did not. */
    const uint64_t last = (uint64_t) addr + (uint64_t) len;
    const uint64_t mask = (uint64_t) line - 1u;
    const uint64_t aligned_end = (last + mask) & ~mask;
    /* AT OR ABOVE 2 TO THE 32ND, NOT ABOVE IT, and the boundary is the point.
     * `end` is one past the last address, so a range ending exactly at the top
     * of the address space has no value a uint32_t can hold: the honest
     * encoding would be 2 to the 32nd and the only available one is zero.
     * Storing zero is not a near miss, it is EXACTLY the failure this finding
     * is about, because a loop from base to end then runs zero times and
     * maintains nothing without a fault or a diagnostic. So the case is
     * refused and the signature says so, rather than returning a number a
     * caller would use. */
    if (aligned_end >= 0x100000000ull) {
        return MPU_ERR_WRAP;
    }

    const uint32_t base = addr & (uint32_t) ~mask;
    out->base = base;
    out->end = (uint32_t) aligned_end;
    out->bytes = out->end - base;
    out->lines = out->bytes / line;
    /* The two shared lines, which finding one is about: the head when the
     * buffer does not start on a line, the tail when it does not end on one. */
    out->head_shared = ((addr & (uint32_t) mask) != 0u) ? 1u : 0u;
    out->tail_shared = ((last & mask) != 0u) ? 1u : 0u;
    return 0;
}

int mpu_buffer_check(uint32_t addr, uint32_t len, uint32_t line)
{
    mpu_range_t r;
    const int rc = mpu_maint_range(addr, len, line, &r);
    if (rc != 0) {
        return rc;
    }
    /* Criterion 4 wants BOTH facts, and finding two is that they fix different
     * ends. A shared line in either position is reported as a failure of
     * alignment, because that is the criterion's own word for it. */
    if (r.head_shared != 0u || r.tail_shared != 0u) {
        return MPU_ERR_ALIGN;
    }
    return 0;
}

int mpu_region_encode(uint32_t base, uint32_t size, uint8_t *size_field)
{
    if (size_field == NULL) {
        return MPU_ERR_ARGUMENT;
    }
    /* No upper test, and the absence is deliberate. Every power of two a
     * uint32_t can hold is at or below 2 to the 31st, which is
     * MPU_MAX_REGION, so a size above it cannot arrive here AND be a power of
     * two. The one region the architecture allows and this signature cannot
     * express is the whole address space, 2 to the 32nd, which cannot arrive
     * as an argument at all. A `size > MPU_MAX_REGION` test would therefore be
     * a guard that can never fire, and a check that cannot fail is worse than
     * no check: it reads as coverage. */
    if (!is_power_of_two(size) || size < MPU_MIN_REGION) {
        return MPU_ERR_SIZE;
    }
    /* Finding three: the base must be a multiple of the region's OWN size and
     * not of the cache line, so a buffer that satisfies criterion 4 can still
     * fail here. */
    if ((base & (size - 1u)) != 0u) {
        return MPU_ERR_ALIGN;
    }

    /* The SIZE field is log2(size) - 1, so the smallest region, 32 bytes,
     * encodes as 4. Counted rather than taken from a table, because a table
     * would need the same arithmetic to be written correctly once anyway. */
    uint32_t log2 = 0u;
    uint32_t v = size;
    while (v > 1u) {
        v >>= 1;
        log2 += 1u;
    }
    *size_field = (uint8_t) (log2 - 1u);
    return 0;
}
