/* latstat.c: the histogram, the ranks, the verdict and the stack arithmetic.
 *
 * Every rounding and every width here is stated, because the twin in
 * ../python/latstat.py has neither a fixed width nor an implicit rounding and
 * the parity test compares the two exactly.
 */
#include "latstat.h"

#include <stddef.h>

int lat_record(lat_hist_t *h, uint32_t cycles, uint32_t cycles_per_us)
{
    if (h == NULL) {
        return LAT_ERR_ARGUMENT;
    }
    if (cycles_per_us == 0u) {
        return LAT_ERR_RATE;
    }
    /* Refused before the sample is taken rather than after the counter wraps.
     * A wrapped count makes every rank below it wrong, and nothing in the
     * printed result would show it. */
    if (h->count == UINT32_MAX) {
        return LAT_ERR_FULL;
    }

    /* TRUNCATES toward zero, which is step 5's own expression and finding two.
     * Both operands are unsigned, so there is no implementation defined
     * direction to argue about; the bias is downward and it is the reason the
     * verdict below refuses to pass a bin that straddles its budget. */
    const uint32_t us = cycles / cycles_per_us;

    if (us < LAT_BINS) {
        h->bins[us] += 1u;
    } else {
        h->over += 1u;
    }
    if (us > h->max_us) {
        h->max_us = us;
    }
    h->count += 1u;
    return 0;
}

/* The ceiling of count times numer over denom, in 64 bits. Finding four: for a
 * count near four thousand million, which this counter holds, 999 times the
 * count overflows a 32-bit product and the rank comes out small, which reports
 * a percentile far below the truth. */
static uint32_t rank_of(uint32_t count, uint32_t numer, uint32_t denom)
{
    const uint64_t product = (uint64_t) count * (uint64_t) numer;
    const uint64_t rank = (product + (uint64_t) denom - 1u) / (uint64_t) denom;
    return (uint32_t) rank;
}

/* The bin holding the sample of the given rank, counting from one. Returns
 * non-zero when the histogram could answer, and zero when the rank falls
 * inside the overflow count, which is finding three. */
static int bin_at_rank(const lat_hist_t *h, uint32_t rank, uint32_t *out)
{
    uint64_t seen = 0u;
    for (uint32_t k = 0u; k < LAT_BINS; k++) {
        seen += (uint64_t) h->bins[k];
        if (seen >= (uint64_t) rank) {
            *out = k;
            return 1;
        }
    }
    return 0;
}

int lat_report(const lat_hist_t *h, lat_report_t *out)
{
    if (h == NULL || out == NULL) {
        return LAT_ERR_ARGUMENT;
    }
    /* A report of zero microseconds would read as the fastest run ever
     * measured, so no samples is a refusal and not a figure. */
    if (h->count == 0u) {
        return LAT_ERR_EMPTY;
    }

    /* The count is checked against the histogram rather than trusted. A
     * histogram that did not come from lat_record, or one whose counter was
     * written separately, gives ranks that point at the wrong samples, and
     * nothing in the output would show it. */
    uint64_t total = (uint64_t) h->over;
    for (uint32_t k = 0u; k < LAT_BINS; k++) {
        total += (uint64_t) h->bins[k];
    }
    if (total != (uint64_t) h->count) {
        return LAT_ERR_COUNT;
    }

    /* The rank definition is PINNED, because the chapter does not state one and
     * two reasonable choices differ by one sample. The percentile is the
     * smallest value at or above which the given fraction of the samples lies:
     * the sample of rank ceil(p * n), counting from one. */
    out->median_us = 0u;
    out->p999_us = 0u;
    out->median_resolved = (uint8_t)
        bin_at_rank(h, rank_of(h->count, 1u, 2u), &out->median_us);
    out->p999_resolved = (uint8_t)
        bin_at_rank(h, rank_of(h->count, 999u, 1000u), &out->p999_us);
    out->max_us = h->max_us;
    out->over = h->over;
    out->count = h->count;
    return 0;
}

/* Finding one: bin k covers the true values from k up to just under k plus
 * one, so only a bin entirely on one side of the budget decides anything. */
static uint8_t decide_bin(uint32_t bin, uint32_t budget)
{
    /* A STRICT COMPARISON AND NOT bin + 1u <= budget, WHICH WRAPPED. The two
     * are the same in arithmetic and not the same in a uint32_t: the maximum
     * reaches this function unbounded by the bin count, lat_record can produce
     * 4294967295 microseconds from a cycle count of 4294967295 at one cycle
     * per microsecond, and bin + 1u then wrapped to zero and returned PASS for
     * the worst run this instrument can record. The twin computes in arbitrary
     * precision and returned FAIL, so the two disagreed and no oracle case had
     * reached the corner. Found by clippy on the Rust twin, which rejects the
     * same expression as int_plus_one, on Wednesday 28 October 2026. */
    if (bin < budget) {
        return (uint8_t) LAT_PASS;
    }
    if (bin > budget) {
        return (uint8_t) LAT_FAIL;
    }
    /* bin == budget: part of this bin meets the budget and part does not, and
     * the truncation in lat_record means the true value is at or above the
     * recorded one. Undecided, which is the finding. */
    return (uint8_t) LAT_UNDECIDED;
}

/* An unresolved percentile is a value at or above LAT_BINS microseconds, which
 * is everything the histogram knows about it. That still fails any budget
 * below LAT_BINS, which is where all three of table 20.4's rows sit. */
static uint8_t decide_unresolved(uint32_t budget)
{
    if (LAT_BINS > budget) {
        return (uint8_t) LAT_FAIL;
    }
    return (uint8_t) LAT_UNDECIDED;
}

int lat_verdict(const lat_report_t *r, const lat_budget_t *b,
                lat_verdict_t *out)
{
    if (r == NULL || b == NULL || out == NULL) {
        return LAT_ERR_ARGUMENT;
    }

    out->median = (r->median_resolved != 0u)
        ? decide_bin(r->median_us, b->median_us)
        : decide_unresolved(b->median_us);
    out->p999 = (r->p999_resolved != 0u)
        ? decide_bin(r->p999_us, b->p999_us)
        : decide_unresolved(b->p999_us);
    /* The maximum is a recorded value and not a bin, but it was truncated by
     * the same division, so it is judged the same way. */
    out->max = decide_bin(r->max_us, b->max_us);
    return 0;
}

int lat_stack(uint32_t budget, uint32_t free_bytes, lat_stack_t *out)
{
    if (out == NULL) {
        return LAT_ERR_ARGUMENT;
    }
    if (budget == 0u) {
        return LAT_ERR_STACK;
    }
    /* Finding five: more free space than the whole budget is impossible, so it
     * means the budget and the kernel's figure came from different tasks.
     * Refused rather than clamped. */
    if (free_bytes > budget) {
        return LAT_ERR_STACK;
    }

    out->used = budget - free_bytes;
    out->permil = (uint32_t) (((uint64_t) out->used * 1000u) / (uint64_t) budget);
    /* Eighty percent as a comparison and not a division, so the rule is exact
     * at the boundary: used over budget at or below four fifths. */
    out->within = (((uint64_t) out->used * 5u) <= ((uint64_t) budget * 4u))
        ? (uint8_t) 1u : (uint8_t) 0u;
    return 0;
}
