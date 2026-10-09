/* projects/P06-timer-sampling/c/rate.h: the witness, which decides whether a rate claim stands.
 *
 * THE HALF OF P06 THAT CAN BE PROVEN. The three acquisition back ends in
 * acq_*.c refuse at run time rather than guessing a converter, timer or
 * transfer engine setting RM0455 governs. The witness does not need any of
 * that: it is arithmetic over a recording of the marker pin, and it is the
 * piece that decides whether a square wave at a plausible wrong rate is
 * reported as a pass.
 *
 * A clean square wave at the wrong rate is indistinguishable from a right one
 * without an external witness. That is this project's whole subject, and it is
 * why the witness is worth four implementations while the back ends are not.
 *
 * TWO INDEPENDENT ROUTES THAT MUST AGREE. A count, which is edges minus one
 * over the span, and a straight-line fit of edge index against edge time. The
 * fit is the reported rate and the count is the check on it. If they disagree,
 * the analysis is wrong and not the firmware, and reporting either number would
 * be reporting a defect in the measurement as a property of the board.
 *
 * ON FLOATING POINT, and the reason this header says anything about it at all.
 * The four implementations are compared to a stated tolerance rather than bit
 * for bit. Double arithmetic is deterministic, but neither the association of a
 * sum nor the contraction of a multiply and an add into one instruction is the
 * same across four toolchains: gcc may contract where rustc does not, and the
 * Cortex-M7 has a fused multiply-add where the host may not. So this file is
 * compiled with -ffp-contract=off, which makes the question moot where it can
 * be made moot, and the comparison allows a relative difference of 1e-12. The
 * pass criteria are 0.1 percent, so that tolerance is nine orders of magnitude
 * tighter than anything the measurement claims.
 *
 * Compiles unchanged for the target, which matters more here than elsewhere:
 * the same witness could run on the board against its own cycle counter.
 */
#ifndef RATE_H
#define RATE_H

#include <stdbool.h>
#include <stddef.h>

/* The criteria from docs/measurement.md, written before any hardware was
 * connected. Changing a number here is a change to the claim, so it is a change
 * to that document too, and to the other three implementations. */
#define RATE_NOMINAL_HZ              1000.0
#define RATE_TOLERANCE_FRACTION      0.001      /* criterion 1: 0.1 percent */
#define RATE_JITTER_SD_MAX_S         10e-6      /* criterion 2 */
#define RATE_WORST_INTERVAL_DEV_MAX_S 50e-6     /* criterion 3 */
#define RATE_MAX_MISSING_EDGES       0          /* criterion 4 */

/* The largest recording this will analyse, and the largest number of edges in
 * it. Fixed rather than allocated: this file compiles for the target, where
 * there is no allocator, and a refusal is better than a malloc. */
#define RATE_MAX_SAMPLES 65536u
#define RATE_MAX_EDGES   4096u

typedef enum {
    RATE_OK                 =  0,
    RATE_ERR_TOO_FEW_EDGES  = -1,  /* fewer than three; nothing to measure */
    RATE_ERR_NO_SWING       = -2,  /* a flat recording has no edges to find */
    RATE_ERR_TOO_MANY_EDGES = -3,  /* more edges than RATE_MAX_EDGES */
    RATE_ERR_ARGS           = -4,
} rate_status_t;

/* Everything docs/measurement.md asks for, and the verdict on each. */
typedef struct {
    double sample_rate_hz;
    double duration_s;
    size_t edges;
    double nominal_hz;
    double resolution_s;

    double count_rate_hz;      /* route one: edges minus one over the span */
    double fit_period_s;       /* route two: the straight-line gradient */
    double fit_rate_hz;
    double fit_rate_se_hz;

    double interval_mean_s;
    double interval_sd_s;
    double interval_worst_s;
    double interval_worst_dev_s;
    size_t missing_edges;

    bool routes_agree;
    bool rate_within_tolerance;
    bool jitter_within_limit;
    bool worst_interval_within_limit;
    bool no_missing_edges;
    bool pass;

    /* True when the measured spread is below half the witness sample period, so
     * it is limited by the instrument and not by the board. A claim can never be
     * finer than the instrument, and saying so in the result beats leaving a
     * reader to work it out. */
    bool limited_by_instrument;
} rate_result_t;

/* Edge times in seconds, rising and falling alike, by linear interpolation
 * across the midpoint of a Schmitt-style pair of thresholds taken from the
 * observed swing rather than assumed to be 0 and 3.3 volts. The marker toggles
 * once per sample, so every crossing in either direction is a sampling
 * instant. Interpolation is what gets an edge time below one sample period,
 * which criterion 2 depends on.
 *
 * Returns the number of edges written, or a negative rate_status_t. */
int rate_marker_edges(const double *samples, size_t count, double fs,
                      double *edges_out, size_t edges_cap);

/* Least squares gradient, intercept and the standard error of the gradient.
 * Written out rather than taken from a library, so the witness needs nothing
 * beyond the standard library on a Raspberry Pi or on the board. */
int rate_fit_line(const double *x, const double *y, size_t count,
                  double *slope, double *intercept, double *slope_se);

/* The whole analysis. Returns RATE_OK and fills *out, or a negative
 * rate_status_t, in which case *out carries what was known before the refusal
 * so a caller can still print the edge count. */
int rate_analyse(const double *samples, size_t count, double fs,
                 double nominal_hz, rate_result_t *out);

const char *rate_status_name(int status);

#endif /* RATE_H */
