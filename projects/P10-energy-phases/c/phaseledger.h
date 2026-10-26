/* phaseledger.h: chapter 10's per-phase charge, and the residual that tests it.
 *
 * WHAT THIS IS. A current trace and a marker code per sample go in; the charge
 * drawn during each phase comes out, together with the total integrated WITHOUT
 * reference to the markers and the residual between the two. The chapter is
 * explicit about why those two totals are computed by different routes: "if the
 * residual were defined as the difference between the phase sum and itself it
 * would always be zero and the test would prove nothing".
 *
 * NONE OF THIS NEEDS THE INSTRUMENT. Charge on a uniformly sampled trace is a sum
 * times the sample interval, and the decode of marker codes into runs is a
 * run-length encoding. Both are exact on synthetic input, so the arithmetic can be
 * settled before the power profiler is ever wired to the board.
 *
 * ONE THING IS DELIBERATELY DIFFERENT FROM THE CHAPTER'S SKETCH, and it is a
 * correction rather than a preference. Step 7 rejects "any interval shorter than
 * the instrument's sample interval" and compares times in seconds:
 *
 *     [iv for iv in out if (iv[2] - iv[1]) >= dt]
 *
 * where the interval ends are products i*dt. **That comparison is decided by
 * floating-point rounding.** Run on a ten sample trace with dt = 1e-5 it keeps two
 * single-sample runs and rejects three others, because 2*dt - 1*dt and 5*dt - 4*dt
 * differ from dt in the last bits and in opposite directions. The physical
 * question it means to ask is whether a run is shorter than the instrument can
 * resolve, which is a question about a count of samples, so **this file counts
 * samples as integers** and takes the threshold in samples.
 *
 * WHAT IS NOT HERE. The marker protocol of step 2 lives on the board; the pins,
 * the single store to the set and reset register, and the bound on what a marker
 * costs are all board work. So is every actual number: this is the arithmetic that
 * will turn a capture into a ledger, and no capture exists yet.
 */
#ifndef PHASELEDGER_H
#define PHASELEDGER_H

#include <stdint.h>

/* Three pins, so eight codes, which is chapter 10's protocol exactly: 0 sleep,
 * 1 wake, 2 sense, 3 compute, 4 send, 7 fault, and 5 and 6 unused. The unused
 * codes are carried rather than refused, because a trace that contains one is a
 * finding and the report should be able to show it. */
#define PHASE_CODES 8u

typedef struct {
    double per_code[PHASE_CODES]; /* charge in coulombs, by marker code       */
    double total;                 /* integrated with no reference to markers  */
    double split;                 /* the sum of the kept runs                 */
    double residual;              /* |total - split| / total, the test itself */
    uint32_t runs;                /* runs kept                                */
    uint32_t rejected;            /* runs dropped as shorter than the floor   */
    uint32_t rejected_samples;    /* samples inside those runs, which is where
                                   * the residual comes from when it is not
                                   * zero: charge drawn in a rejected run is
                                   * counted in the total and not in the split */
} phase_ledger_t;

#define PHASE_ERR_ARGUMENT (-1)   /* a null pointer, or no samples            */
#define PHASE_ERR_INTERVAL (-2)   /* a sample interval of zero or below       */
#define PHASE_ERR_FLOOR    (-3)   /* a rejection floor of zero samples        */
#define PHASE_ERR_CODE     (-4)   /* a marker code above seven                */

/* Returns 0, or one of the refusals above having written nothing.
 *
 * min_samples is the shortest run that counts as a phase. One keeps every run,
 * which is the chapter's stated intent without its rounding problem; two rejects
 * single-sample runs, which is what an instrument that samples at the same rate
 * the markers change cannot distinguish from a glitch.
 *
 * THE RESIDUAL IS THE ACCEPTANCE TEST AND IT IS EXPECTED TO BE NON-ZERO when runs
 * are rejected: their charge is in the total and not in the split, by
 * construction. The chapter's step 9 asks for exactly that attribution, and
 * rejected_samples above is what makes it checkable rather than a guess. */
int phase_ledger(const double *current_a, const uint8_t *codes, uint32_t n,
                 double dt_s, uint32_t min_samples, phase_ledger_t *out);

#endif /* PHASELEDGER_H */
