/* latstat.h: chapter 20's latency histogram, its percentiles and the verdict
 * against the budget table, which together are the chapter's acceptance.
 *
 * WHAT THIS IS, AND WHY IT NEEDS NO KERNEL. Chapter 20's third acceptance
 * criterion is "a million release events are recorded with the timer source,
 * and the report gives the median, the 99.9th percentile and the maximum, with
 * the overflow count shown so the tail can be trusted", and its fourth is "all
 * three latency figures meet the budget written in the budget table above". The
 * figures come from the board. THE ARITHMETIC THAT TURNS THEM INTO A VERDICT
 * DOES NOT, and it is the part that decides whether the chapter passes. It is
 * here, it is integer throughout, and it is proven against a twin on a laptop
 * with no board, no FreeRTOS and no shield.
 *
 * Table 20.4 is the budget: a median of 100 microseconds, a 99.9th percentile
 * of 250 and a maximum of 1 millisecond, all written before the first run. Step
 * 5 fixes the instrument: 1024 bins, one microsecond per bin, a running maximum
 * and an overflow counter.
 *
 * FINDING ONE: A PERCENTILE READ FROM A HISTOGRAM IS A BIN AND NOT A VALUE, AND
 * THE BUDGET ROWS SIT EXACTLY ON A BIN BOUNDARY. Bin k holds every sample whose
 * microsecond value truncated to a whole number is k, so the true values in it
 * run from k up to just under k plus one. The median budget is 100 and the bin
 * width is 1, so a median landing in bin 100 cannot be compared with a budget
 * of 100 by this instrument: part of that bin meets the budget and part of it
 * does not. This library therefore returns THREE verdicts and not two, and the
 * third one, undecided, is the honest answer rather than a missing feature. A
 * pass needs the whole bin below the budget and a failure needs the whole bin
 * above it.
 *
 * FINDING TWO: THE TRUNCATION BIASES TOWARD PASSING, WHICH IS THE OTHER HALF OF
 * FINDING ONE. Step 5 computes microseconds as cycles divided by cycles per
 * microsecond, which truncates toward zero, so every sample is recorded at or
 * below its true value by up to one microsecond. A verdict that took the
 * recorded value at face value would therefore pass runs that did not meet the
 * budget. Undecided at the boundary is what that truncation costs, and it is
 * cheaper than a wrong pass.
 *
 * FINDING THREE: THE OVERFLOW COUNTER CHANGES A PERCENTILE'S TYPE AND NOT ONLY
 * ITS TAIL. The chapter keeps the overflow count "so the tail can be trusted"
 * and then reads its percentiles from the histogram. If a percentile's rank
 * falls inside the overflow count there is no bin to report, and the only
 * honest answer is that the percentile is unresolved. This library returns that
 * as a flag and leaves the value alone, because reporting the top bin, 1023,
 * for a sample that is at least 1024 is the one wrong answer nothing
 * downstream can detect. An unresolved percentile still decides the verdict
 * where it can: at least 1024 microseconds fails any budget below 1024.
 *
 * FINDING FOUR: THE RANK ARITHMETIC OVERFLOWS 32 BITS LONG BEFORE THE COUNTER
 * DOES. The 99.9th percentile's rank is the ceiling of 999 times the count over
 * 1000. A million events makes 999 million, which fits. This counter is 32 bits
 * and holds four thousand million, where 999 times the count overflows a
 * 32-bit product and the rank comes out small, reporting a percentile far below
 * the truth. The product here is 64-bit, the oracle carries a count the 32-bit
 * product cannot take, and the twin has no such limit, so this is exactly the
 * divergence an equivalence test exists to catch.
 *
 * FINDING FIVE: THE KERNEL REPORTS FREE STACK AND THE BUDGET TABLE WANTS USED.
 * Criterion 6 is that "every task's stack high-water mark is reported in the
 * same result line, and no task has used more than eighty percent of its
 * budget". uxTaskGetStackHighWaterMark returns the SMALLEST FREE SPACE the task
 * has ever had, so used is the budget less that number. Reporting the kernel's
 * figure in a column headed "used" turns a task at eighty percent into a task
 * at twenty, which is the direction that hides the problem. A free figure above
 * the budget is refused rather than clamped, because it means the two numbers
 * came from different tasks.
 *
 * WHAT IS NOT HERE. FreeRTOS, the four tasks, the notification and semaphore
 * wakes, the mutex, the supervisor and its watchdog, every latency figure,
 * every stack high-water figure and the processor load. Those need the board,
 * the IKS4A1 shield and a seventeen minute run. There is no measured number in
 * this project. The budgets below are quoted from table 20.4, which was written
 * before any measurement, and the verdict function is what will judge the
 * figures when they arrive.
 */
#ifndef LATSTAT_H
#define LATSTAT_H

#include <stdint.h>

/* Step 5's instrument: one bin per microsecond, and the chapter's number. */
#define LAT_BINS 1024u

/* Table 20.4's three latency rows, in microseconds, quoted and not chosen. */
#define LAT_BUDGET_MEDIAN 100u
#define LAT_BUDGET_P999   250u
#define LAT_BUDGET_MAX    1000u

typedef struct {
    uint32_t bins[LAT_BINS];
    uint32_t over;   /* samples at or above LAT_BINS microseconds          */
    uint32_t max_us; /* running maximum, which the histogram cannot hold   */
    uint32_t count;  /* every sample, including the ones in over           */
} lat_hist_t;

typedef struct {
    uint32_t median_us;      /* the bin, meaningful only when resolved     */
    uint32_t p999_us;
    uint8_t median_resolved; /* zero when the rank fell inside over        */
    uint8_t p999_resolved;
    uint32_t max_us;
    uint32_t over;
    uint32_t count;
} lat_report_t;

typedef struct {
    uint32_t median_us;
    uint32_t p999_us;
    uint32_t max_us;
} lat_budget_t;

/* Three verdicts, and the third one is finding one. */
#define LAT_PASS      0u
#define LAT_FAIL      1u
#define LAT_UNDECIDED 2u

typedef struct {
    uint8_t median;
    uint8_t p999;
    uint8_t max;
} lat_verdict_t;

typedef struct {
    uint32_t used;   /* the budget less the kernel's free figure           */
    uint32_t permil; /* used per thousand of the budget, truncated         */
    uint8_t within;  /* non-zero when used is at or below eighty percent   */
} lat_stack_t;

#define LAT_ERR_ARGUMENT (-1) /* a null pointer                            */
#define LAT_ERR_RATE     (-2) /* zero cycles per microsecond, which would
                               * divide by zero rather than scale          */
#define LAT_ERR_FULL     (-3) /* the sample counter is at its maximum; one
                               * more would wrap it, and every percentile
                               * computed afterwards would be wrong        */
#define LAT_ERR_EMPTY    (-4) /* no samples, so there is no percentile; a
                               * report of zero would read as a fast run   */
#define LAT_ERR_COUNT    (-5) /* the count disagrees with the bins plus the
                               * overflow, so the histogram was not built by
                               * lat_record and the ranks mean nothing     */
#define LAT_ERR_STACK    (-6) /* a zero budget, or a free figure larger than
                               * the budget, which means the two numbers
                               * came from different tasks                 */

/* One sample. cycles_per_us scales the core's cycle counter, and the division
 * TRUNCATES, which finding two is about. */
int lat_record(lat_hist_t *h, uint32_t cycles, uint32_t cycles_per_us);

/* The median bin, the 99.9th percentile's bin, the maximum and the overflow.
 * Each percentile carries a flag saying whether the histogram could answer. */
int lat_report(const lat_hist_t *h, lat_report_t *out);

/* The three budget rows judged. A pass needs the whole bin below the budget, a
 * failure needs the whole bin above it, and anything else is undecided. */
int lat_verdict(const lat_report_t *r, const lat_budget_t *b,
                lat_verdict_t *out);

/* One task's stack, from the budget and the kernel's smallest free figure. */
int lat_stack(uint32_t budget, uint32_t free_bytes, lat_stack_t *out);

#endif /* LATSTAT_H */
