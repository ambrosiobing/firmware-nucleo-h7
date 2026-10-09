/* projects/P06-timer-sampling/c/rate.c: the witness, written out in full.
 *
 * Every operation is in the same order as python/firmkit/rate.py, which is the
 * reference the other three were written against. That is not stylistic: a sum
 * accumulated in a different order gives a different double, and the parity
 * test compares these numbers. Where the Python writes a comprehension, this
 * writes the loop that walks the same elements the same way.
 *
 * No library beyond sqrt and fabs. The fit is written out rather than taken
 * from anywhere, so this compiles for the target and for a Raspberry Pi with
 * nothing installed.
 */
#include "rate.h"

#include <math.h>

/* nan("") and not NAN, in all nine places, and the reason is a warning rather
   than a preference. C defines NAN as a constant expression of type FLOAT, so
   every conditional in this file that offered it as one arm promoted a float to
   a double, and -Wdouble-promotion said so four times when gcc 15 first saw this
   file in WSL on bing@JPTOUPM678 on Sunday 4 October 2026. Nothing would have
   gone wrong, because a float NaN is still a NaN, but the whole subject of this
   file is that its arithmetic is double and agrees with three other
   implementations to one part in 1e12, so a warning that finds a float in an
   arithmetic path is one to keep rather than one to cast away. nan("") returns a
   double, gcc folds it to a constant, newlib provides it for the target, and it
   is what rate.hpp already wrote, which is why the C++ never warned and the C
   did. The two files now read the same. */
#include <stddef.h>

int rate_marker_edges(const double *samples, size_t count, double fs,
                      double *edges_out, size_t edges_cap)
{
    if (samples == NULL || edges_out == NULL || fs <= 0.0) {
        return RATE_ERR_ARGS;
    }
    /* Sixteen samples is the Python's own floor. Fewer than that is not a
     * recording of a square wave, it is a fragment. */
    if (count < 16u) {
        return 0;
    }

    double lo_obs = samples[0];
    double hi_obs = samples[0];
    for (size_t i = 1; i < count; i++) {
        if (samples[i] < lo_obs) lo_obs = samples[i];
        if (samples[i] > hi_obs) hi_obs = samples[i];
    }
    const double swing = hi_obs - lo_obs;
    if (swing <= 0.0) {
        return RATE_ERR_NO_SWING;
    }

    /* Thresholds from the observed swing rather than assumed rail voltages, so
     * a recording through an attenuator or with an offset still works. */
    const double low  = lo_obs + 0.3 * swing;
    const double high = lo_obs + 0.7 * swing;
    const double mid  = 0.5 * (low + high);

    /* The marker toggles, so a crossing in EITHER direction is a sampling
     * instant and both are kept. The hysteresis is the Schmitt pair: a rise
     * counts only from an established low, a fall only from an established
     * high. The starting level is the first sample against the midpoint, and
     * nothing is counted until the first crossing. Until Friday 9 October 2026
     * this kept rising edges alone, to match a marker pulse the witness could
     * not see; see marker.c. */
    size_t written = 0;
    bool is_high = samples[0] >= mid;

    for (size_t i = 1; i < count; i++) {
        const double prev = samples[i - 1];
        const double cur  = samples[i];
        const bool rise = !is_high && cur >= high;
        const bool fall = is_high && cur < low;
        if (!rise && !fall) {
            continue;
        }
        double frac;
        if (cur != prev) {
            frac = (mid - prev) / (cur - prev);
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;
        } else {
            frac = 0.0;
        }
        if (written >= edges_cap) {
            return RATE_ERR_TOO_MANY_EDGES;
        }
        edges_out[written++] = ((double) (i - 1) + frac) / fs;
        is_high = rise;
    }
    return (int) written;
}

int rate_fit_line(const double *x, const double *y, size_t count,
                  double *slope, double *intercept, double *slope_se)
{
    if (x == NULL || y == NULL || slope == NULL || intercept == NULL
        || slope_se == NULL) {
        return RATE_ERR_ARGS;
    }
    if (count < 3u) {
        *slope = *intercept = *slope_se = nan("");
        return RATE_ERR_TOO_FEW_EDGES;
    }

    const double n = (double) count;
    double sx = 0.0, sy = 0.0;
    for (size_t i = 0; i < count; i++) { sx += x[i]; }
    for (size_t i = 0; i < count; i++) { sy += y[i]; }
    const double mx = sx / n;
    const double my = sy / n;

    double sxx = 0.0;
    for (size_t i = 0; i < count; i++) {
        const double d = x[i] - mx;
        sxx += d * d;
    }
    if (sxx == 0.0) {
        *slope = *intercept = *slope_se = nan("");
        return RATE_ERR_TOO_FEW_EDGES;
    }

    double sxy = 0.0;
    for (size_t i = 0; i < count; i++) {
        sxy += (x[i] - mx) * (y[i] - my);
    }
    const double m = sxy / sxx;
    const double c = my - m * mx;

    /* The residual sum, in the same order the Python walks it. */
    double ss = 0.0;
    for (size_t i = 0; i < count; i++) {
        const double r = y[i] - (m * x[i] + c);
        ss += r * r;
    }
    const double s2 = ss / (n - 2.0);

    *slope = m;
    *intercept = c;
    *slope_se = sqrt(s2 / sxx);
    return RATE_OK;
}

int rate_analyse(const double *samples, size_t count, double fs,
                 double nominal_hz, rate_result_t *out)
{
    if (samples == NULL || out == NULL) {
        return RATE_ERR_ARGS;
    }

    static double edges[RATE_MAX_EDGES];
    static double index[RATE_MAX_EDGES];

    /* Everything knowable before the edges are found, so a refusal still
     * carries the sample rate and the duration. */
    *out = (rate_result_t) {0};
    out->sample_rate_hz = fs;
    out->duration_s = (fs > 0.0) ? (double) count / fs : 0.0;
    out->nominal_hz = nominal_hz;
    out->resolution_s = (fs > 0.0) ? 1.0 / fs : nan("");

    const int found = rate_marker_edges(samples, count, fs, edges, RATE_MAX_EDGES);
    if (found < 0) {
        return found;
    }
    out->edges = (size_t) found;
    if (found < 3) {
        out->pass = false;
        return RATE_ERR_TOO_FEW_EDGES;
    }

    const size_t n = (size_t) found;

    /* Route one: the count. */
    const double span = edges[n - 1] - edges[0];
    out->count_rate_hz = (span > 0.0) ? ((double) (n - 1)) / span : nan("");

    /* Route two: the fit. */
    for (size_t i = 0; i < n; i++) {
        index[i] = (double) i;
    }
    double period = nan(""), intercept = nan(""), period_se = nan("");
    (void) rate_fit_line(index, edges, n, &period, &intercept, &period_se);
    out->fit_period_s = period;
    out->fit_rate_hz = (period != 0.0) ? 1.0 / period : nan("");
    /* dRate = dPeriod / period squared. */
    out->fit_rate_se_hz = (period != 0.0) ? period_se / (period * period) : nan("");

    /* The intervals, and what the worst one is. */
    const size_t ivs = n - 1u;
    double sum_iv = 0.0;
    for (size_t i = 0; i < ivs; i++) {
        sum_iv += edges[i + 1] - edges[i];
    }
    const double mean_iv = sum_iv / (double) ivs;
    out->interval_mean_s = mean_iv;

    double var = 0.0;
    if (ivs > 1u) {
        double acc = 0.0;
        for (size_t i = 0; i < ivs; i++) {
            const double d = (edges[i + 1] - edges[i]) - mean_iv;
            acc += d * d;
        }
        var = acc / (double) (ivs - 1u);
    }
    out->interval_sd_s = sqrt(var);

    const double nominal_iv = 1.0 / nominal_hz;
    double worst = edges[1] - edges[0];
    double worst_dev = fabs(worst - nominal_iv);
    for (size_t i = 1; i < ivs; i++) {
        const double v = edges[i + 1] - edges[i];
        const double dev = fabs(v - nominal_iv);
        /* Strictly greater, so the first of two equally bad intervals wins,
         * which is what Python's max does. */
        if (dev > worst_dev) {
            worst = v;
            worst_dev = dev;
        }
    }
    out->interval_worst_s = worst;
    out->interval_worst_dev_s = worst_dev;

    /* A dropped edge shows as an interval near an integer multiple of nominal.
     * A doubled edge shows as one near a half. Both are missing edges. */
    size_t missing = 0;
    for (size_t i = 0; i < ivs; i++) {
        const double v = edges[i + 1] - edges[i];
        const double k = v / nominal_iv;
        /* round half away from zero, which is what Python's round does NOT do:
         * Python rounds half to even. The difference can only matter when k is
         * exactly a half integer, which a real recording never produces and a
         * synthetic one could. nearbyint with the default rounding mode rounds
         * half to even and therefore matches. */
        const double nearest = nearbyint(k);
        if (nearest >= 2.0 && fabs(k - nearest) < 0.25) {
            missing += (size_t) nearest - 1u;
        } else if (nearest == 0.0 || (k < 0.75 && k > 0.0)) {
            missing += 1u;
        }
    }
    out->missing_edges = missing;

    /* The two routes must agree, or the analysis is wrong and not the firmware. */
    out->routes_agree = !isnan(out->fit_rate_hz)
        && fabs(out->count_rate_hz - out->fit_rate_hz) < 0.01 * nominal_hz;

    out->rate_within_tolerance =
        fabs(out->fit_rate_hz - nominal_hz) <= RATE_TOLERANCE_FRACTION * nominal_hz;
    out->jitter_within_limit = out->interval_sd_s <= RATE_JITTER_SD_MAX_S;
    out->worst_interval_within_limit =
        out->interval_worst_dev_s <= RATE_WORST_INTERVAL_DEV_MAX_S;
    out->no_missing_edges = out->missing_edges <= (size_t) RATE_MAX_MISSING_EDGES;

    out->pass = out->rate_within_tolerance && out->jitter_within_limit
        && out->worst_interval_within_limit && out->no_missing_edges
        && out->routes_agree;

    out->limited_by_instrument = out->interval_sd_s < out->resolution_s / 2.0;
    return RATE_OK;
}

const char *rate_status_name(int status)
{
    switch (status) {
    case RATE_OK:                 return "ok";
    case RATE_ERR_TOO_FEW_EDGES:  return "too_few_edges";
    case RATE_ERR_NO_SWING:       return "no_swing";
    case RATE_ERR_TOO_MANY_EDGES: return "too_many_edges";
    case RATE_ERR_ARGS:           return "args";
    default:                      return "?";
    }
}
