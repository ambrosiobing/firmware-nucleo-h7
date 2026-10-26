/* dspaccept.c: three formats of one cascade, and the two metrics that judge them.
 *
 * The coefficient order is b0 b1 b2 a1 a2 per stage with the denominator terms
 * already negated, which is the library's order and the chapter's warning.
 *
 * Every shift here is written as a floor division rather than as >>, because a
 * right shift of a negative signed value is implementation defined in C while
 * the fixed-point routine this models truncates toward negative infinity. The
 * division says which of the two is meant.
 */
#include "dspaccept.h"

#include <math.h>
#include <stddef.h>

static double round_half_away_from_zero(double v)
{
    return (v < 0.0) ? -floor(-v + 0.5) : floor(v + 0.5);
}

/* 32768 / 2^shift, built by halving so it is exact in binary at every shift. */
static double q15_scale(int32_t post_shift)
{
    double scale = 32768.0;
    for (int32_t i = 0; i < post_shift; i++) {
        scale *= 0.5;
    }
    return scale;
}

/* What an arithmetic right shift of bits places does, spelled as the floor
 * division it is, so the sign behaviour is in the source rather than in the
 * compiler's choice. */
static int64_t floor_shift(int64_t v, uint32_t bits)
{
    const int64_t d = (int64_t) 1 << bits;
    int64_t q = v / d;
    if (v < 0 && (v % d) != 0) {
        q -= 1;
    }
    return q;
}

static int check_cascade(const void *coeffs, uint32_t nstage, const void *in,
                         const void *out, uint32_t n)
{
    if (coeffs == NULL || in == NULL || out == NULL || n == 0u) {
        return DSP_ERR_ARGUMENT;
    }
    if (nstage == 0u || nstage > BIQUAD_MAX_STAGE) {
        return DSP_ERR_STAGES;
    }
    return 0;
}

int biquad_df2t_f64(const double *coeffs, uint32_t nstage,
                    const double *in, double *out, uint32_t n)
{
    const int bad = check_cascade(coeffs, nstage, in, out, n);
    if (bad != 0) {
        return bad;
    }
    for (uint32_t c = 0u; c < nstage * BIQUAD_COEFF_PER_STAGE; c++) {
        if (!isfinite(coeffs[c])) {
            return DSP_ERR_FINITE;
        }
    }

    for (uint32_t i = 0u; i < n; i++) {
        out[i] = in[i];
    }

    /* Stage by stage over the whole block, which is how the library does it and
     * what makes the state two words rather than four. */
    for (uint32_t s = 0u; s < nstage; s++) {
        const double b0 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 0u];
        const double b1 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 1u];
        const double b2 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 2u];
        const double a1 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 3u];
        const double a2 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 4u];
        double d1 = 0.0;
        double d2 = 0.0;
        for (uint32_t i = 0u; i < n; i++) {
            const double x = out[i];
            const double y = b0 * x + d1;
            d1 = b1 * x + a1 * y + d2;
            d2 = b2 * x + a2 * y;
            out[i] = y;
        }
    }
    return 0;
}

int biquad_df2t_f32(const float *coeffs, uint32_t nstage,
                    const float *in, float *out, uint32_t n)
{
    const int bad = check_cascade(coeffs, nstage, in, out, n);
    if (bad != 0) {
        return bad;
    }
    for (uint32_t c = 0u; c < nstage * BIQUAD_COEFF_PER_STAGE; c++) {
        if (!isfinite(coeffs[c])) {
            return DSP_ERR_FINITE;
        }
    }

    for (uint32_t i = 0u; i < n; i++) {
        out[i] = in[i];
    }

    for (uint32_t s = 0u; s < nstage; s++) {
        const float b0 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 0u];
        const float b1 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 1u];
        const float b2 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 2u];
        const float a1 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 3u];
        const float a2 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 4u];
        float d1 = 0.0f;
        float d2 = 0.0f;
        for (uint32_t i = 0u; i < n; i++) {
            const float x = out[i];
            const float y = b0 * x + d1;
            d1 = b1 * x + a1 * y + d2;
            d2 = b2 * x + a2 * y;
            out[i] = y;
        }
    }
    return 0;
}

int q15_post_shift(const double *coeffs, uint32_t nstage, int32_t *out)
{
    if (coeffs == NULL || out == NULL) {
        return DSP_ERR_ARGUMENT;
    }
    if (nstage == 0u || nstage > BIQUAD_MAX_STAGE) {
        return DSP_ERR_STAGES;
    }

    const uint32_t total = nstage * BIQUAD_COEFF_PER_STAGE;
    for (uint32_t c = 0u; c < total; c++) {
        if (!isfinite(coeffs[c])) {
            return DSP_ERR_FINITE;
        }
    }

    for (int32_t shift = 0; shift <= Q15_MAX_POST_SHIFT; shift++) {
        const double scale = q15_scale(shift);
        int fits = 1;
        for (uint32_t c = 0u; c < total; c++) {
            const double v = round_half_away_from_zero(coeffs[c] * scale);
            if (v < -32768.0 || v > 32767.0) {
                fits = 0;
                break;
            }
        }
        if (fits != 0) {
            *out = shift;
            return 0;
        }
    }
    return DSP_ERR_RANGE;
}

int q15_coeffs(const double *coeffs, uint32_t nstage, int32_t post_shift,
               int16_t *out)
{
    if (coeffs == NULL || out == NULL) {
        return DSP_ERR_ARGUMENT;
    }
    if (nstage == 0u || nstage > BIQUAD_MAX_STAGE) {
        return DSP_ERR_STAGES;
    }
    if (post_shift < 0 || post_shift > Q15_MAX_POST_SHIFT) {
        return DSP_ERR_SHIFT;
    }

    const uint32_t total = nstage * BIQUAD_COEFF_PER_STAGE;
    const double scale = q15_scale(post_shift);
    for (uint32_t c = 0u; c < total; c++) {
        if (!isfinite(coeffs[c])) {
            return DSP_ERR_FINITE;
        }
    }
    /* Checked in full before anything is written, so a refusal leaves the
     * caller's array untouched rather than half converted. */
    for (uint32_t c = 0u; c < total; c++) {
        const double v = round_half_away_from_zero(coeffs[c] * scale);
        if (v < -32768.0 || v > 32767.0) {
            return DSP_ERR_RANGE;
        }
    }
    for (uint32_t c = 0u; c < total; c++) {
        out[c] = (int16_t) round_half_away_from_zero(coeffs[c] * scale);
    }
    return 0;
}

int biquad_df1_q15(const int16_t *coeffs, uint32_t nstage, int32_t post_shift,
                   const int16_t *in, int16_t *out, uint32_t n,
                   uint32_t *saturations)
{
    if (coeffs == NULL || in == NULL || out == NULL || saturations == NULL
        || n == 0u) {
        return DSP_ERR_ARGUMENT;
    }
    if (nstage == 0u || nstage > BIQUAD_MAX_STAGE) {
        return DSP_ERR_STAGES;
    }
    if (post_shift < 0 || post_shift > Q15_MAX_POST_SHIFT) {
        return DSP_ERR_SHIFT;
    }

    *saturations = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        out[i] = in[i];
    }

    /* The product of a Q15 coefficient and a Q15 sample is Q30, and the result
     * wanted is Q15, so the accumulator is shifted down by 15 and back up by the
     * post shift the coefficients were scaled by: 15 - post_shift places. */
    const uint32_t bits = (uint32_t) (15 - post_shift);

    for (uint32_t s = 0u; s < nstage; s++) {
        const int64_t b0 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 0u];
        const int64_t b1 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 1u];
        const int64_t b2 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 2u];
        const int64_t a1 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 3u];
        const int64_t a2 = coeffs[s * BIQUAD_COEFF_PER_STAGE + 4u];
        int64_t x1 = 0;
        int64_t x2 = 0;
        int64_t y1 = 0;
        int64_t y2 = 0;
        for (uint32_t i = 0u; i < n; i++) {
            const int64_t x = out[i];
            const int64_t acc = b0 * x + b1 * x1 + b2 * x2 + a1 * y1 + a2 * y2;
            int64_t y = floor_shift(acc, bits);
            if (y > 32767) {
                y = 32767;
                (*saturations)++;
            } else if (y < -32768) {
                y = -32768;
                (*saturations)++;
            }
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            out[i] = (int16_t) y;
        }
    }
    return 0;
}

int accept_metrics(const double *got, uint32_t n_got,
                   const double *ref, uint32_t n_ref, accept_t *out)
{
    if (got == NULL || ref == NULL || out == NULL || n_got == 0u) {
        return DSP_ERR_ARGUMENT;
    }
    if (n_got != n_ref) {
        /* Not a verdict on the measurement but a fault in the call. Comparing
         * the first n_ref of one block against another is how a truncated
         * capture becomes a passing acceptance test. */
        return DSP_ERR_ARGUMENT;
    }
    const uint32_t n = n_got;
    for (uint32_t i = 0u; i < n; i++) {
        if (!isfinite(got[i]) || !isfinite(ref[i])) {
            /* A diverged fixed-point cascade or a reference with a hole in it is
             * named here rather than carried into a tolerance comparison, where
             * a not-a-number compares false against every bound and reads as an
             * ordinary failure. */
            return DSP_ERR_FINITE;
        }
    }

    /* The means, rather than the sums, because that is how the chapter's script
     * spells both metrics and a twin of a script should be comparable to it. */
    double mean_err2 = 0.0;
    double mean_ref2 = 0.0;
    for (uint32_t i = 0u; i < n; i++) {
        const double e = got[i] - ref[i];
        mean_err2 += e * e;
        mean_ref2 += ref[i] * ref[i];
    }
    mean_err2 /= (double) n;
    mean_ref2 /= (double) n;

    if (mean_ref2 == 0.0) {
        return DSP_ERR_REFERENCE;
    }

    if (mean_err2 == 0.0) {
        out->rel_rms = 0.0;
        out->snr_db = (double) INFINITY;
        out->exact = 1;
        return 0;
    }

    out->rel_rms = sqrt(mean_err2) / sqrt(mean_ref2);
    out->snr_db = 10.0 * log10(mean_ref2 / mean_err2);
    out->exact = 0;
    return 0;
}
