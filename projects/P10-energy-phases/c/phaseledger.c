/* phaseledger.c: run-length decode the markers, integrate each run, reconcile.
 *
 * The run lengths are counts of samples and never differences of seconds, for
 * the reason the header gives at length. Everything else is the chapter's
 * arithmetic unchanged.
 */
#include "phaseledger.h"

#include <stddef.h>

static double magnitude(double v)
{
    return (v < 0.0) ? -v : v;
}

int phase_ledger(const double *current_a, const uint8_t *codes, uint32_t n,
                 double dt_s, uint32_t min_samples, phase_ledger_t *out)
{
    if (current_a == NULL || codes == NULL || out == NULL || n == 0u) {
        return PHASE_ERR_ARGUMENT;
    }
    if (!(dt_s > 0.0)) {
        return PHASE_ERR_INTERVAL;
    }
    if (min_samples == 0u) {
        return PHASE_ERR_FLOOR;
    }
    for (uint32_t i = 0u; i < n; i++) {
        if ((uint32_t) codes[i] >= PHASE_CODES) {
            return PHASE_ERR_CODE;
        }
    }

    for (uint32_t c = 0u; c < PHASE_CODES; c++) {
        out->per_code[c] = 0.0;
    }
    out->runs = 0u;
    out->rejected = 0u;
    out->rejected_samples = 0u;

    /* The total, integrated first and with no reference to the markers at all.
     * The chapter insists on this: a residual computed between the phase sum and
     * itself would always be zero and would prove nothing. */
    double total = 0.0;
    for (uint32_t i = 0u; i < n; i++) {
        total += current_a[i];
    }
    total *= dt_s;

    /* The runs, by index rather than by time, which is the whole correction this
     * file makes to the chapter's sketch. */
    double split = 0.0;
    uint32_t start = 0u;
    for (uint32_t i = 1u; i <= n; i++) {
        if (i == n || codes[i] != codes[start]) {
            const uint32_t count = i - start;
            if (count >= min_samples) {
                double q = 0.0;
                for (uint32_t k = start; k < i; k++) {
                    q += current_a[k];
                }
                q *= dt_s;
                out->per_code[codes[start]] += q;
                split += q;
                out->runs++;
            } else {
                out->rejected++;
                out->rejected_samples += count;
            }
            start = i;
        }
    }

    out->total = total;
    out->split = split;
    out->residual = (total != 0.0)
                  ? (magnitude(total - split) / magnitude(total))
                  : 1.0;
    return 0;
}
