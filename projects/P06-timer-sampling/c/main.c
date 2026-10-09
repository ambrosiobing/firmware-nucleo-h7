/* main.c: byte identical across all three builds.
 *
 * That is the whole discipline of this chapter. If this file had to know which
 * acquisition back end it was talking to, the comparison in
 * docs/measurement.md would be comparing three programs rather than three
 * mechanisms, and the result would mean nothing.
 *
 * It does almost nothing on purpose: start the acquisition, drain blocks,
 * report. The interesting code is behind acq.h and the interesting numbers
 * come from the witness on the Raspberry Pi, not from here.
 */
#include <stdint.h>
#include <stdio.h>

#include "acq.h"
#include "cyccnt.h"

/* Provided by chapter 1: the clock tree, the console and the printf retarget.
 * Chapter 6 adds nothing to any of them. */
extern void board_init(void);

static void banner(void)
{
    /* Everything the witness metadata needs to match a capture to a build.
     * The back end name matches the --build argument of witness/scan.py
     * exactly, so the firmware and the capture cannot disagree about what was
     * running. */
    printf("\r\n");
    printf("nucleo-h7a3-sampling\r\n");
    printf("  acquisition   %s\r\n", acq_name());
    printf("  nominal rate  %u Hz\r\n", (unsigned) ACQ_RATE_HZ);
    printf("  instrument    %s\r\n", cyccnt_backend_name());

    uint32_t hz = cyccnt_tick_hz();
    if (hz == 0u) {
        /* Refusing to print a rate is correct. A tick count without its rate
         * is not a time, and a guessed rate would scale every measured figure
         * by an unknown factor. */
        printf("  tick rate     not established, timings not measured\r\n");
    } else {
        printf("  tick rate     %u Hz\r\n", (unsigned) hz);
    }
}

int main(void)
{
    board_init();
    cyccnt_init();
    banner();

    int rc = acq_start();
    if (rc != 0) {
        /* Every back end returns a negative value rather than running with a
         * guessed peripheral setting. Say which and stop: a board that runs
         * and lies is worse than one that refuses. */
        printf("acq_start failed: %d\r\n", rc);
        /* WHAT THIS USED TO SAY WAS TRUE WHEN IT WAS WRITTEN AND IS NOT NOW. It
         * told the reader that "a value is unconfirmed against RM0455" and to go
         * find the TO BE CONFIRMED comments. That was right while every refusal
         * was an unconfirmed value, and the timer back end's refusals are now
         * mostly flag timeouts: ADCAL never cleared, ADRDY never set, a register
         * that did not take. A reader sent looking for a TO BE CONFIRMED comment
         * would find none and conclude the message was stale rather than that the
         * converter had refused. Corrected Friday 9 October 2026. */
        printf("the code is named in acq_errors.h, which also says which back\r\n");
        printf("end owns it. Read the step reports above first: the step that\r\n");
        printf("refused printed what it wrote and what the register held.\r\n");
        for (;;) { }
    }

    /* WHETHER THE CONVERTER'S OWN OVERRUN FLAG IS WATCHED, SAID ONCE AND BEFORE
     * THE FIRST REPORT LINE, so the convovr column below cannot be misread.
     *
     * This is printed here rather than in banner() because acq_start is what
     * enables the line, so before that call the answer is not yet true. The
     * negative case states what a zero would and would not have meant, because
     * the whole reason this field exists is that the run on Wednesday 7 October
     * 2026 reported overruns 0 for 390 blocks while nothing watched this flag. */
    uint32_t probe = 0u;
    if (acq_conv_overruns(&probe) == 0) {
        printf("  converter overrun  watched, so convovr below is a real count\r\n");
    } else {
        printf("  converter overrun  NOT WATCHED by this back end, so convovr\r\n");
        printf("                     reads -1 rather than a zero nobody earned\r\n");
    }

    uint32_t blocks = 0u;
    uint32_t last_report = 0u;

    for (;;) {
        acq_block_t b;
        if (acq_take(&b) == 0) {
            continue;              /* nothing ready; the tight loop is fine here */
        }
        blocks++;

        /* The application does something with the samples, because a build
         * that discards them would not be exercising the path under test. The
         * cheapest honest thing is a running sum, which the compiler cannot
         * remove because it is reported. */
        uint32_t sum = 0u;
        for (uint32_t i = 0u; i < b.count; i++) {
            sum += b.samples[i];
        }

        /* Report about once a second, with the threshold computed from the
         * nominal rate rather than from a delay, so no software delay sits in
         * the path being measured.
         *
         * WHAT THIS COMMENT USED TO CLAIM, AND IT WAS WRONG. It said the
         * reporting cadence "does not itself depend on the thing being
         * measured". The THRESHOLD does not; the cadence in time depends on the
         * actual sampling rate entirely, because a line is emitted every
         * ACQ_RATE_HZ / count blocks, which is every 960 samples, whenever those
         * samples happen to arrive. On Wednesday 7 October 2026 that sentence
         * led the project README to publish a prediction that "blocks rises by
         * roughly 15 each line", which is 1000 / 64 and could not have come out
         * otherwise whatever the board did. Half a prediction that cannot fail
         * is worse than none, because it reads as confirmation. */
        if (blocks - last_report >= (ACQ_RATE_HZ / b.count)) {
            last_report = blocks;

            /* Two overrun numbers side by side, because they answer different
             * questions and either can be zero while the other is not. overruns
             * is main being too slow to collect a finished block. convovr is the
             * converter unable to deliver a conversion because the previous
             * result had not been read out of DR, which is the interrupt being
             * too slow or not arriving. A long convovr with overruns 0 would mean
             * the handler is losing samples the application never hears about. */
            uint32_t covr = 0u;
            const long covr_shown =
                (acq_conv_overruns(&covr) == 0) ? (long) covr : -1L;

            printf("seq %lu  blocks %lu  mean %lu  overruns %lu  convovr %ld\r\n",
                   (unsigned long) b.seq, (unsigned long) blocks,
                   (unsigned long) (sum / b.count),
                   (unsigned long) acq_overruns(),
                   covr_shown);
        }
    }
}
