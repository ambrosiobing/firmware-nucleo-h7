/* projects/P03-interrupt-receive/c/rx_ring.c: the path that keeps the bytes. Built with -DRX_PATH=ring.
 *
 * This is the producer half of P02's ring buffer. P02 built the structure and
 * made the ordering argument; this supplies the one context that calls
 * ring_put, which is the receive interrupt.
 *
 * THE RULE THAT MAKES IT WORK, and it is P02's invariant restated: exactly one
 * context may call ring_put. That is this handler and nothing else. A second
 * interrupt source calling it, at a different priority, breaks every guarantee
 * P02 argued for, and the symptom is data that is fine for hours and then wrong
 * once.
 *
 * THE RULE THAT MAKES IT CORRECT. The receive interrupt is enabled once and
 * never disabled. That is the repair for the first documented failure mode in
 * faults.c: there is no window between a completion and a re-arm in which a byte
 * can be lost, because there is no completion. One byte per interrupt, pushed
 * somewhere that cannot block, and the count belongs to the application.
 *
 * THE HANDLER IS FIFTEEN LINES and must stay that way. It reads the status, takes
 * one byte, pushes it, clears what needs clearing, and returns. No printf, which
 * is not reentrant. No parsing, which is P05's job. Anything else belongs in the
 * main loop, reached through the ring.
 */
#include "rx.h"
#include "ring.h"
#include "usart3_ll.h"

static ring_t    g_rx;
static rx_stats_t g_stats;

int rx_start(void)
{
    ring_init(&g_rx);
    g_stats = (rx_stats_t){ 0 };

    /* Every one of these refuses while the values are unconfirmed, and each
     * names its own document rather than failing generically. */
    const int rc = usart3_init_receive();
    if (rc != RX_OK) {
        return rc;
    }
    return RX_OK;
}

/* The interrupt. Named from the vector table in c/board/startup.c, whose
 * position for this peripheral is itself TO BE CONFIRMED against RM0455. */
void USART3_IRQHandler(void)
{
    g_stats.handler_calls++;

    const uint32_t status = usart3_status();

    /* The error path first, and the clear before the return. This is the repair
     * for the second documented failure mode: an overrun flag left set makes the
     * interrupt re-assert the instant this returns, and the handler then runs
     * for ever while the main loop starves. On this family the clear is a write
     * to a dedicated register, not a read of the status register, which is
     * exactly what material written for an older family gets wrong. */
    if (usart3_has_overrun(status)) {
        g_stats.overruns++;
#ifndef RX_FAULT_OVERRUN
        usart3_clear_overrun();
#endif
    }
    if (usart3_has_framing_error(status)) {
        g_stats.framing++;
        usart3_clear_framing_error();
    }
    if (usart3_has_noise_error(status)) {
        g_stats.noise++;
        usart3_clear_noise_error();
    }

    if (usart3_has_byte(status)) {
        /* Reading the data register is what clears the byte-ready condition, so
         * the read must happen whether or not the ring accepts it. A handler
         * that returns without reading leaves the condition true and re-enters,
         * which is the same liveness failure as the uncleared overrun. */
        const uint8_t b = usart3_read_byte();

        if (ring_put(&g_rx, b)) {
            g_stats.accepted++;
        } else {
            /* Counted, not hidden. A buffer that silently refuses bytes looks
             * exactly like a link that works. */
            g_stats.dropped++;
        }
    }

#ifdef RX_FAULT_REARM
    /* The deliberate defect: disable our own interrupt after a count, as the
     * library's receive call does, and let the main loop re-arm it. Every byte
     * arriving in that window is lost with no flag and no counter. */
    static uint32_t since_rearm;
    if (++since_rearm >= 16u) {
        since_rearm = 0u;
        usart3_disable_receive_interrupt();
    }
#endif
}

int rx_get(uint8_t *b)
{
    const int got = ring_get(&g_rx, b);
    if (got) {
        /* Read the occupancy from the consumer, which is the only context
         * allowed to without racing itself. */
        const uint32_t used = ring_used(&g_rx);
        if (used > g_stats.peak_used) {
            g_stats.peak_used = used;
        }
    }
#ifdef RX_FAULT_REARM
    usart3_enable_receive_interrupt();   /* the re-arm, from the wrong place */
#endif
    return got;
}

const rx_stats_t *rx_stats(void) { return &g_stats; }
const char *rx_name(void)        { return "ring"; }
