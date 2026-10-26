/* cyclecharge.h: chapter 7's segmentation, its per-cycle report, its battery sum.
 *
 * WHAT THIS IS, AND THE CHAPTER ASKED FOR IT IN THESE WORDS. Step 9: "Test the
 * analysis on a synthetic trace. The segmentation is the fragile part, so it is
 * tested on the host with a trace whose answer is known by construction, in
 * continuous integration, with no instrument attached." That is this file. Step 8
 * segments a current trace into cycles on a marker bit and reports the median and
 * the extremes; step 10 turns one cycle's energy into a battery lifetime and
 * insists the measurement and the arithmetic stay apart.
 *
 * HOW THIS DIFFERS FROM P10, which also integrates a current trace. P10 decodes a
 * marker CODE carried on every sample and attributes charge to a phase. This
 * segments on marker EDGES, cycle to cycle, and reports the spread across cycles,
 * which is the thing the chapter says decides a battery life: "an average current
 * over a duty-cycled trace hides the thing that decides a battery life, which is
 * whether the cycles are alike".
 *
 * FINDING ONE: THE CHAPTER'S OWN STEP 9 TEST ASSERTS THE WRONG COUNT. Run as
 * printed on the trace it builds, per_cycle_charge returns EIGHT cycles and the
 * test asserts nine. The trace begins with the marker already high, so the first
 * cycle has no rising edge in front of it: the construction yields nine rises, and
 * nine rises bound eight complete cycles. The comment beside the assertion reads
 * "nine complete cycles in ten". A test whose expected value is wrong is worse
 * than no test, because it is the thing a reader checks the code against.
 *
 * FINDING TWO: THE WINDOW IS OFF BY ONE SAMPLE, AND THAT TEST CANNOT SEE IT.
 * np.diff puts a rising edge at index i where d[i] is 0 and d[i+1] is 1, and the
 * sum then runs current[a:b] from that index, so every cycle carries one sample
 * belonging to the cycle before it. On the flat 100 microamp trace the test uses,
 * one sample of 100 microamps replaces another and the charge is unchanged, which
 * is exactly why it passes. Put one large sample on a boundary and the cycle's
 * charge moves from 100.000 to 109.999 microcoulombs. The windows here begin at
 * the first sample that is actually inside the cycle.
 *
 * FINDING THREE: THE LIFETIME LEAVES MOST OF THE DAY FREE. Step 10's arithmetic
 * takes cycles_per_day as an assumption while the trace already fixes the cycle
 * period, and nothing checks that the two agree. A one second cycle at 100 cycles
 * a day covers 100 seconds of 86400, and the other 86300 cost nothing at all in
 * that sum. At a plausible two microamps of sleep the printed form reports 73636
 * days where costing the uncovered day gives 4033: a factor of 18. At 1440 cycles
 * a day the factor is 2.2. The chapter's list of what the arithmetic does not
 * model names self-discharge, pulsed capacity, temperature, regulator efficiency
 * and the fact that the board is not the product, and does not name this. So
 * cyc_lifetime() reports the covered and uncovered seconds, costs the remainder
 * at a stated sleep current, and reports the printed form beside its own.
 *
 * WHAT IS NOT HERE. The instrument and its GPL interface, the resistor check that
 * gives it an uncertainty, the run-state inventory, the stop-mode entry sequence
 * from RM0455, the clock restore and the two traces that show what it costs, the
 * low-power timer variant, the tickless variant, and the three marker pins. There
 * is no measured number in this project: every trace it has seen was synthetic.
 */
#ifndef CYCLECHARGE_H
#define CYCLECHARGE_H

#include <stdint.h>

/* Enough cycles for the chapter's "at least ten" with room to spare, and a bound
 * so a caller can put the array on its stack. */
#define CYC_MAX_CYCLES 64u

/* The chapter's own floor, as a default a caller may raise: "Report the median
 * and the extremes over at least ten cycles, never a single average current." */
#define CYC_MIN_CYCLES 10u

#define CYC_SECONDS_PER_DAY 86400.0

typedef struct {
    uint32_t cycles;
    double median_uc;
    double min_uc;
    double max_uc;
    double spread_pct;        /* 100 * (max - min) / median, as step 8 defines */
    double energy_median_uj;  /* the median charge at the stated supply        */
} cyc_report_t;

typedef struct {
    double energy_per_cycle_uj; /* THE MEASUREMENT. Everything below is not.  */
    double cycles_per_day;      /* an assumption                               */
    double cycle_period_s;      /* from the trace, which is why it is here     */
    double sleep_ua;            /* an assumption: what the uncovered day costs */
    double sleep_volts;         /* an assumption                               */
    double cell_mah;            /* an assumption                               */
    double cell_volts;          /* an assumption                               */
} cyc_battery_t;

typedef struct {
    double cell_j;
    double covered_s;           /* cycles_per_day * cycle_period_s             */
    double uncovered_s;         /* the rest of the day, which step 10 omits    */
    double cycle_j_per_day;
    double uncovered_j_per_day;
    double days;                /* with the uncovered day costed               */
    double days_chapter;        /* step 10's printed form, for comparison      */
    int covers_the_day;         /* 1 when uncovered_s is zero                  */
} cyc_battery_out_t;

#define CYC_ERR_ARGUMENT (-1) /* a null pointer, or no samples                */
#define CYC_ERR_RATE     (-2) /* a sample rate at or below zero               */
#define CYC_ERR_BIT      (-3) /* a marker bit outside a byte                  */
#define CYC_ERR_ROOM     (-4) /* more cycles in the trace than the caller's array */
#define CYC_ERR_CYCLES   (-5) /* fewer complete cycles than the floor allows  */
#define CYC_ERR_SUPPLY   (-6) /* a supply, cell or period at or below zero    */
#define CYC_ERR_DAY      (-7) /* cycles that claim more than a day            */

/* Charge in microcoulombs for each COMPLETE cycle, where a cycle runs from one
 * rising edge of the marker bit to the next. A trace that begins with the marker
 * already high has no rising edge in front of its first cycle, so that cycle is
 * not complete and is not reported: that is the whole of finding one. */
int cyc_per_cycle_charge(const double *current_ua, const uint8_t *digital,
                         uint32_t n, double fs_hz, uint8_t cycle_bit,
                         double *out_uc, uint32_t max_out, uint32_t *count);

/* The median, the extremes and the spread, refusing fewer cycles than the floor
 * because the chapter's instruction to report at least ten is a rule and not a
 * suggestion. The median of an even count is the mean of the two middle values,
 * which is what the chapter's library does. */
int cyc_report(const double *charge_uc, uint32_t count, double volts,
               uint32_t min_cycles, cyc_report_t *out);

/* Arithmetic only, with every assumption an argument and the uncovered part of
 * the day costed rather than free. */
int cyc_lifetime(const cyc_battery_t *in, cyc_battery_out_t *out);

#endif /* CYCLECHARGE_H */
