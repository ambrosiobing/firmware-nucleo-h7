/* cyclecharge.c: segment on the marker's rising edges, report, then cost a day.
 *
 * The header says at length why the window starts where it does, why a trace
 * that opens with the marker high has one fewer complete cycle than it looks,
 * and why the uncovered part of a day is costed here rather than left free.
 */
#include "cyclecharge.h"

#include <stddef.h>

static double median_of(double *sorted, uint32_t count)
{
    /* The mean of the two middle values on an even count, which is what the
     * chapter's library does and therefore what a comparison against it needs. */
    if ((count & 1u) != 0u) {
        return sorted[count / 2u];
    }
    return 0.5 * (sorted[count / 2u - 1u] + sorted[count / 2u]);
}

static void sort_small(double *v, uint32_t count)
{
    /* An insertion sort over at most CYC_MAX_CYCLES values. No allocation, and
     * it is the same choice P12's gate made for the same reason. */
    for (uint32_t i = 1u; i < count; i++) {
        const double key = v[i];
        uint32_t j = i;
        while (j > 0u && v[j - 1u] > key) {
            v[j] = v[j - 1u];
            j--;
        }
        v[j] = key;
    }
}

int cyc_per_cycle_charge(const double *current_ua, const uint8_t *digital,
                         uint32_t n, double fs_hz, uint8_t cycle_bit,
                         double *out_uc, uint32_t max_out, uint32_t *count)
{
    if (current_ua == NULL || digital == NULL || out_uc == NULL
        || count == NULL || n == 0u) {
        return CYC_ERR_ARGUMENT;
    }
    if (!(fs_hz > 0.0)) {
        return CYC_ERR_RATE;
    }
    if (cycle_bit > 7u) {
        return CYC_ERR_BIT;
    }

    *count = 0u;

    /* The first sample of each cycle, which is the sample at which the marker
     * is high having been low. A trace that begins high gives no such index for
     * its first cycle, and that cycle is therefore incomplete and unreported. */
    uint32_t previous_start = 0u;
    int have_start = 0;

    for (uint32_t i = 1u; i < n; i++) {
        const uint32_t was = ((uint32_t) digital[i - 1u] >> cycle_bit) & 1u;
        const uint32_t now = ((uint32_t) digital[i] >> cycle_bit) & 1u;
        if (was != 0u || now == 0u) {
            continue;
        }
        /* i is the first sample inside a cycle, not the last one outside it. */
        if (have_start != 0) {
            if (*count >= max_out) {
                return CYC_ERR_ROOM;
            }
            double sum = 0.0;
            for (uint32_t k = previous_start; k < i; k++) {
                sum += current_ua[k];
            }
            out_uc[*count] = sum / fs_hz;
            (*count)++;
        }
        previous_start = i;
        have_start = 1;
    }
    return 0;
}

int cyc_report(const double *charge_uc, uint32_t count, double volts,
               uint32_t min_cycles, cyc_report_t *out)
{
    if (charge_uc == NULL || out == NULL) {
        return CYC_ERR_ARGUMENT;
    }
    if (count > CYC_MAX_CYCLES) {
        return CYC_ERR_ROOM;
    }
    if (!(volts > 0.0)) {
        return CYC_ERR_SUPPLY;
    }
    if (count < min_cycles || count == 0u) {
        /* The chapter's floor, enforced: a median and a spread over three
         * cycles is a number that looks like the one it asks for and is not. */
        return CYC_ERR_CYCLES;
    }

    double sorted[CYC_MAX_CYCLES];
    for (uint32_t i = 0u; i < count; i++) {
        sorted[i] = charge_uc[i];
    }
    sort_small(sorted, count);

    out->cycles = count;
    out->min_uc = sorted[0];
    out->max_uc = sorted[count - 1u];
    out->median_uc = median_of(sorted, count);
    out->energy_median_uj = out->median_uc * volts;
    if (out->median_uc == 0.0) {
        /* Every cycle drew nothing, so the spread has no denominator. Reported
         * as zero rather than as a division, and the medians beside it say why. */
        out->spread_pct = 0.0;
    } else {
        out->spread_pct = 100.0 * (out->max_uc - out->min_uc) / out->median_uc;
    }
    return 0;
}

int cyc_lifetime(const cyc_battery_t *in, cyc_battery_out_t *out)
{
    if (in == NULL || out == NULL) {
        return CYC_ERR_ARGUMENT;
    }
    if (!(in->energy_per_cycle_uj > 0.0) || !(in->cell_mah > 0.0)
        || !(in->cell_volts > 0.0) || !(in->sleep_volts > 0.0)
        || !(in->cycle_period_s > 0.0) || !(in->cycles_per_day > 0.0)) {
        return CYC_ERR_SUPPLY;
    }
    if (in->sleep_ua < 0.0) {
        return CYC_ERR_SUPPLY;
    }

    out->covered_s = in->cycles_per_day * in->cycle_period_s;
    if (out->covered_s > CYC_SECONDS_PER_DAY) {
        /* More cycling than there is day to cycle in. Refused rather than
         * reported, because the rest of this sum would be about a day that does
         * not exist. */
        return CYC_ERR_DAY;
    }
    out->uncovered_s = CYC_SECONDS_PER_DAY - out->covered_s;
    out->covers_the_day = (out->uncovered_s == 0.0) ? 1 : 0;

    out->cell_j = in->cell_mah * 1e-3 * 3600.0 * in->cell_volts;
    out->cycle_j_per_day = in->energy_per_cycle_uj * 1e-6 * in->cycles_per_day;
    out->uncovered_j_per_day = in->sleep_ua * 1e-6 * in->sleep_volts
                             * out->uncovered_s;

    /* Step 10's printed form, which divides by the cycles alone. */
    out->days_chapter = out->cell_j / out->cycle_j_per_day;

    const double total = out->cycle_j_per_day + out->uncovered_j_per_day;
    out->days = out->cell_j / total;
    return 0;
}
