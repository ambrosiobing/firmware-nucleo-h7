/* dspaccept.h: chapter 18's acceptance test, the half that needs no board.
 *
 * WHAT THIS IS. A biquad cascade in three numeric formats over the same
 * coefficients and the same input, plus the two acceptance metrics the chapter
 * judges them by. Nothing here is a transform library: it is the part of chapter
 * 18 that decides whether a result is trustworthy, which is the deliverable the
 * chapter names, and it runs on a host with no CMSIS-DSP, no SciPy and no
 * NUCLEO-H7A3ZI-Q attached.
 *
 * WHY IT CAN BE SETTLED WITHOUT THE BOARD. The chapter's own order is fixed:
 * "the acceptance test is written first, the tolerance is written down before
 * anything is measured, and only then does anyone look at cycle counts". The
 * tolerance is therefore the first deliverable, and whether a numeric format can
 * reach it is a question about arithmetic on a stored vector. The cycle counts,
 * the cache states and the code and data placements are the board half and are
 * not here.
 *
 * THE COEFFICIENT ORDER IS THE CMSIS ORDER, five per stage, b0 b1 b2 a1 a2, with
 * the two denominator terms ALREADY NEGATED relative to the transfer function.
 * The chapter calls that negation "the single most common porting mistake" and it
 * is the reason this file states the order in both the header and the test rather
 * than once.
 *
 * ONE THING THE CHAPTER DOES NOT MENTION DECIDES ITS Q15 VERDICT, and it is a
 * correction rather than a preference. Step 3 exports the design's sections
 * exactly as the design library prints them, which leaves the whole passband gain
 * in the first section's numerator: for a fourth-order Butterworth low pass at
 * 40 Hz of 1000 Hz, b0 of section one is 1.832e-4 while b1 of section two is 2.0.
 * Those two cannot share a Q15 scale. At the post shift the large coefficient
 * forces, the small ones quantise to 2, 3 and 2 parts in 32768, and the measured
 * signal to error ratio of that cascade is 15.02 dB on the chapter's own vector
 * against a tolerance of 55 dB, and -15.77 dB on an impulse, where the error is
 * larger than the signal. Redistributing the identical filter's gain between the
 * sections moves the same measurement to 51.35, 54.37 or 55.24 dB. So the
 * chapter's Q15 criterion is decided by a scaling choice it never states, and
 * q15_post_shift() and q15_coeffs() are separate, callable and refusing for that
 * reason: the scale is an input to the verdict and belongs in the report beside
 * it, exactly as the chapter already requires of a cycle count.
 *
 * WHAT IS NOT HERE. The real transform and its packed output, the complex
 * magnitude, the cycle counter, the eight-case placement grid, the ITCM and DTCM
 * sections and every actual cycle count. Those need the part. There is no
 * measured cycle anywhere in this project, and the status line says host.
 */
#ifndef DSPACCEPT_H
#define DSPACCEPT_H

#include <stdint.h>

/* Eight stages is sixteenth order, far past anything this volume designs, and a
 * bound is what lets a caller put the state on the stack. */
#define BIQUAD_MAX_STAGE 8u
#define BIQUAD_COEFF_PER_STAGE 5u

/* A post shift above seven would mean the coefficients are more than 128 times
 * out of range, which is a design that has not been scaled rather than a scale
 * that needs choosing. Fifteen is the arithmetic limit; seven is the useful one,
 * and refusing between them reports a design error as a design error. */
#define Q15_MAX_POST_SHIFT 7

typedef struct {
    double rel_rms;  /* ||got - ref|| / ||ref||, the float32 criterion       */
    double snr_db;   /* 10 log10 (sum ref^2 / sum err^2), the Q15 criterion  */
    int exact;       /* 1 when every sample agreed, so the error energy is
                      * zero and snr_db is infinite rather than large. The
                      * chapter's sketch divides by that zero; a caller here
                      * reads this field and prints "exact" instead.         */
} accept_t;

#define DSP_ERR_ARGUMENT  (-1) /* a null pointer, or no samples               */
#define DSP_ERR_STAGES    (-2) /* zero stages, or more than BIQUAD_MAX_STAGE  */
#define DSP_ERR_SHIFT     (-3) /* a post shift outside 0 to Q15_MAX_POST_SHIFT */
#define DSP_ERR_RANGE     (-4) /* a coefficient that does not fit that shift  */
#define DSP_ERR_FINITE    (-5) /* a coefficient that is not a finite number   */
#define DSP_ERR_REFERENCE (-6) /* a reference whose energy is zero, so neither
                                * metric can be formed. Refused rather than
                                * reported, because an acceptance test against
                                * silence is not an acceptance test.          */

/* The reference path. Double precision, transposed direct form 2, two state
 * words per stage, state zeroed on entry. This is what the other two are judged
 * against, and the chapter is explicit that the reference is computed in double
 * precision "so the comparison has somewhere to stand". */
int biquad_df2t_f64(const double *coeffs, uint32_t nstage,
                    const double *in, double *out, uint32_t n);

/* The same cascade in float32, which is the format the part has hardware for. */
int biquad_df2t_f32(const float *coeffs, uint32_t nstage,
                    const float *in, float *out, uint32_t n);

/* The smallest post shift in 0 to Q15_MAX_POST_SHIFT at which every coefficient
 * fits in Q15, or DSP_ERR_RANGE when none does. Separate from the conversion
 * because the answer is a property of the design and belongs in the report. */
int q15_post_shift(const double *coeffs, uint32_t nstage, int32_t *out);

/* Convert at a stated shift, refusing any coefficient that does not fit. The
 * shift is an argument rather than an inference so that a caller can ask what a
 * particular scaling costs, which is the experiment the header describes. */
int q15_coeffs(const double *coeffs, uint32_t nstage, int32_t post_shift,
               int16_t *out);

/* Direct form 1 in Q15, four state words per stage, the form CMSIS uses for this
 * format. saturations counts every sample clamped at either rail, and may not be
 * null: it is the number that separates case 2.0 of the scaling sweep from case
 * 1.5, and the chapter's acceptance script has nowhere to print it.
 *
 * The accumulator here is 64-bit, which isolates coefficient and state
 * quantisation from accumulator width. The library's Q15 routine accumulates in
 * 32 bits, so a cascade that saturates its accumulator on the part can differ
 * from this, and that difference is a bench measurement rather than a claim. */
int biquad_df1_q15(const int16_t *coeffs, uint32_t nstage, int32_t post_shift,
                   const int16_t *in, int16_t *out, uint32_t n,
                   uint32_t *saturations);

/* Both metrics in one pass, over double precision inputs so a caller converts
 * once and compares once.
 *
 * BOTH LENGTHS ARE ARGUMENTS, and that is deliberate rather than conventional.
 * The library's own routines take one length and trust the caller, and an
 * earlier version of this one did too. It was wrong here: comparing two blocks
 * of different lengths against the shorter of them returns a plausible number
 * rather than an error, which is the exact failure chapter 18 opens by naming,
 * and the Python twin could refuse it where this could not. A mismatch is a
 * fault in the call and is refused as one. */
int accept_metrics(const double *got, uint32_t n_got,
                   const double *ref, uint32_t n_ref, accept_t *out);

#endif /* DSPACCEPT_H */
