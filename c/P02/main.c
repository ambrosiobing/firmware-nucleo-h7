/* c/P02/main.c: the ring buffer on the target, which is the only place its
 * deliverable can be tested.
 *
 * P02's subject is the memory ordering argument, and the host cannot test it. A
 * laptop running two threads on a machine with a different memory model answers a
 * question about that machine. This runs the producer in thread mode and the
 * consumer in an exception handler on the Cortex-M7 itself.
 *
 * WHAT THIS IS ACTUALLY ARRANGED TO FIND OUT, because the received answer and the
 * likely answer differ and the difference is the chapter.
 *
 * The usual advice for a single producer, single consumer ring is to put a DMB
 * between writing the data and publishing the index. That advice is about two
 * observers that can see each other's stores out of order. Here there is one core.
 * A core sees its own stores in program order, and taking an exception on the same
 * core is a context-synchronising event, so the producer's data write cannot be
 * observed by its own interrupt handler after the index write that followed it.
 *
 * If that reasoning holds, mode 1, a compiler barrier alone, is sufficient for
 * thread to interrupt on this part, and the DMB in mode 2 costs cycles for nothing.
 * The DMB earns its place when the other observer is a bus master rather than an
 * interrupt, which is P04 with a transfer engine and P19 with the cache, and
 * neither is built yet.
 *
 * That reasoning is NOT asserted here. This measures two things and states both
 * plainly:
 *
 *   1. Whether a mismatch is observed, with the number of bytes that passed
 *      through stated beside it. Not observing a race in N operations is not proof
 *      that none exists, and the report says so rather than printing a tick.
 *
 *   2. What each ordering choice costs, in core cycles per ring_put, measured with
 *      DWT_CYCCNT. That one is a measurement rather than an absence, and it is the
 *      number the chapter has never had.
 *
 * Mode 0, with no barrier at all, is the control. It is not shippable and the
 * point of building it is to see whether the compiler reorders the two stores when
 * nothing forbids it.
 *
 * HOW CORRECTNESS IS CHECKED. The producer writes a running counter's low byte and
 * advances it only when ring_put succeeds, so the delivered stream is contiguous
 * even though the ring drops when full. The consumer keeps its own counter and
 * compares. Any delivered byte out of sequence is a mismatch, and a mismatch means
 * either the data was observed before it was written or the ring itself is wrong.
 */
#include <stdio.h>

#include "board.h"
#include "ring.h"
#include "stm32h7a3_regs.h"

/* Shared between thread mode and the handler. This is the object under test. */
static ring_t g_ring;

/* The consumer's state, written only by SysTick_Handler. volatile because thread
 * mode reads them while the handler writes them, and without it the compiler may
 * hoist a read out of the reporting loop and print the same figures forever. */
static volatile uint32_t g_consumed;
static volatile uint32_t g_mismatches;
static volatile uint8_t  g_expect;

void SysTick_Handler(void);

void SysTick_Handler(void)
{
    /* Drain whatever is present. Bounded by the ring size so that a handler can
     * never run longer than one pass, however fast the producer is: an interrupt
     * that cannot return starves the thread that feeds it, and the symptom is a
     * board that looks hung rather than one that reports a problem. */
    uint8_t b;
    uint32_t guard = RING_SIZE;
    while (guard-- > 0u && ring_get(&g_ring, &b) == 1) {
        if (b != g_expect) {
            g_mismatches++;
            /* Resynchronise, so one mismatch does not report as thousands and
             * drown the count that matters. */
            g_expect = b;
        }
        g_expect = (uint8_t) (g_expect + 1u);
        g_consumed++;
    }
}

/* Cycles per ring_put for this build's ordering mode, measured with the consumer
 * stopped so nothing competes for the bus.
 *
 * Two-point, for the reason P01 learned the hard way on Saturday 3 October 2026: a
 * single timed window includes the cost of starting and stopping the measurement,
 * and dividing that by the iteration count silently inflates the figure. Timing n
 * and 2n and subtracting leaves exactly n iterations, and the fixed cost cancels
 * without anybody having to estimate it. A warm-up pass comes first because the
 * first run of a loop pays flash wait states the second does not, and that one does
 * not cancel: it subtracts. */
static uint32_t cycles_per_put(void)
{
    const uint32_t n = 2000u;

    if (!board_cycles_available()) {
        return 0u;
    }

    /* A named sink rather than a compound literal in the loop. Taking the address
     * of a compound literal is valid C, and it also puts an object of automatic
     * storage duration inside the measured window for no reason. The figure being
     * measured is small enough that a few cycles of bookkeeping would show. */
    uint8_t sink = 0u;

    ring_init(&g_ring);
    for (uint32_t i = 0u; i < n; i++) {
        (void) ring_put(&g_ring, (uint8_t) i);
        (void) ring_get(&g_ring, &sink);
    }

    ring_init(&g_ring);
    const uint32_t a0 = board_cycles_now();
    for (uint32_t i = 0u; i < n; i++) {
        (void) ring_put(&g_ring, (uint8_t) i);
        (void) ring_get(&g_ring, &sink);
    }
    const uint32_t a1 = board_cycles_now();

    ring_init(&g_ring);
    const uint32_t b0 = board_cycles_now();
    for (uint32_t i = 0u; i < (2u * n); i++) {
        (void) ring_put(&g_ring, (uint8_t) i);
        (void) ring_get(&g_ring, &sink);
    }
    const uint32_t b1 = board_cycles_now();

    const uint32_t shortw = a1 - a0;
    const uint32_t longw  = b1 - b0;
    if (longw <= shortw) {
        return 0u;
    }
    /* Hundredths of a cycle, so a difference of a fraction between modes is
     * visible without pulling floating point formatting into the image. */
    return ((longw - shortw) * 100u) / n;
}

int main(void)
{
    board_init();

    printf("\r\nnucleo-h7a3 P02, the ring buffer on the target\r\n");
    printf("  ordering      %s\r\n", ring_barrier_name());
    printf("  ring size     %u bytes\r\n", (unsigned) RING_SIZE);

    const uint32_t hz = board_core_hz();
    if (hz == 0u) {
        printf("  core clock    not established, so no figure here is a time\r\n");
    } else {
        printf("  core clock    %lu Hz\r\n", (unsigned long) hz);
    }

    /* The cost, measured before the consumer is started so that the two
     * measurements do not contend. One put and one get per iteration, because a
     * ring with only a producer fills after RING_SIZE bytes and then measures the
     * refusal path instead of the publishing path. */
    const uint32_t cx100 = cycles_per_put();
    if (cx100 == 0u) {
        printf("  cost          not measured: the cycle counter is unavailable\r\n");
    } else {
        printf("  cost          %lu.%02lu cycles per put and get pair\r\n",
               (unsigned long) (cx100 / 100u), (unsigned long) (cx100 % 100u));
        printf("                two-point with a warm-up, consumer stopped\r\n");
    }

    /* Now the concurrency. The consumer is SysTick, which is architectural and
     * needs nothing RM0455 governs: the control bits come from ARM's core_cm7.h.
     *
     * The reload is chosen so the handler fires often relative to a put without
     * starving thread mode. At 64 MHz a reload of 2000 is about 32 kHz, and the
     * handler drains a short burst each time while the producer manages tens of
     * puts between interrupts. Too fast and the board livelocks in the handler;
     * too slow and the ring simply fills and the race never has a chance. */
    ring_init(&g_ring);
    g_consumed = 0u;
    g_mismatches = 0u;
    g_expect = 0u;

    SYST_RVR = 2000u - 1u;
    SYST_CVR = 0u;
    SYST_CSR = (1u << SYST_CSR_CLKSOURCE_POS)
             | (1u << SYST_CSR_TICKINT_POS)
             | (1u << SYST_CSR_ENABLE_POS);

    printf("  consumer      SysTick at reload %u, about %lu Hz\r\n",
           2000u, (unsigned long) (hz ? hz / 2000u : 0u));
    printf("  producing     thread mode, reporting every few seconds\r\n");

    uint8_t next = 0u;
    uint32_t produced = 0u;

    for (;;) {
        /* A batch, then a report. The batch size is large enough that the report
         * is a small fraction of the time and does not dominate the mix. */
        for (uint32_t i = 0u; i < 200000u; i++) {
            if (ring_put(&g_ring, next) == 1) {
                next = (uint8_t) (next + 1u);
                produced++;
            }
        }

        const uint32_t consumed = g_consumed;
        const uint32_t bad      = g_mismatches;

        printf("  %-10lu produced  %-10lu consumed  %-8lu dropped  %lu MISMATCH\r\n",
               (unsigned long) produced,
               (unsigned long) consumed,
               (unsigned long) ring_drops(&g_ring),
               (unsigned long) bad);

        if (bad == 0u) {
            printf("                no mismatch in %lu delivered bytes. That is not\r\n",
                   (unsigned long) consumed);
            printf("                proof of correctness, only an absence so far.\r\n");
        }
    }
}
