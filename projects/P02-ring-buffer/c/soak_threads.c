/* P02/soak_threads.c: two real threads through the ring, for a few seconds.
 *
 * WHAT THIS CAN FIND. A genuine logic fault: a masking error, an off-by-one at
 * the capacity, a counter that does not survive its own wrap, a drop that is not
 * counted. It costs a few lines and it is worth having.
 *
 * WHAT THIS CANNOT FIND, and the program says so in its own output rather than
 * leaving a reader to infer a guarantee it cannot give: an ordering fault. The
 * host is a strongly ordered x86, and it will run a build with RING_BARRIER=0,
 * every barrier removed, for a week without complaint. Passing here is therefore
 * not evidence that the barriers are correctly placed, and it is not evidence
 * that they are unnecessary either.
 *
 * The ordering argument comes from the architecture reference manual for this
 * profile, from Arm's barrier application note, and from the published
 * correctness proof under the C11 memory model. The only machine that can
 * falsify it is the Cortex-M7, with the producer in an interrupt handler, which
 * is P03.
 *
 * Built by tools/build_host.py and driven from tests/test_ring.py.
 */
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ring.h"

#define TARGET_BYTES 20000000u

static ring_t   g_ring;
static uint64_t g_accepted;      /* producer thread only */
static uint64_t g_refused;       /* producer thread only */
static uint64_t g_delivered;     /* consumer thread only */
static int      g_order_fault;   /* consumer thread only */

/* xorshift32. Chosen over the linear congruential generator the chapter's
 * listing used, because that one's low bits have a period of eight and the
 * burst size was taken modulo 264, which is eight times thirty-three. The
 * burst sizes therefore carried a period-eight structure and crossed the
 * buffer boundaries far less randomly than they appeared to. */
static uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void *producer(void *arg)
{
    (void) arg;
    uint32_t s = 0x1234567u;
    while (g_accepted < TARGET_BYTES) {
        uint32_t n = xs32(&s) % (RING_SIZE + 8u);
        for (uint32_t i = 0; i < n; i++) {
            if (ring_put(&g_ring, (uint8_t) (g_accepted & 0xFFu))) {
                g_accepted++;
            } else {
                g_refused++;
            }
        }
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void) arg;
    uint32_t s = 0x89abcdefu;
    uint8_t  b = 0u;
    while (g_delivered < TARGET_BYTES) {
        uint32_t n = xs32(&s) % (RING_SIZE + 8u);
        for (uint32_t i = 0; i < n; i++) {
            if (!ring_get(&g_ring, &b)) {
                break;
            }
            if (b != (uint8_t) (g_delivered & 0xFFu)) {
                g_order_fault = 1;
                return NULL;
            }
            g_delivered++;
        }
    }
    return NULL;
}

int main(void)
{
    pthread_t tp, tc;

    ring_init(&g_ring);
    printf("two-thread soak, ordering: %s\n", ring_barrier_name());
    printf("target %u bytes through a %u byte ring\n",
           (unsigned) TARGET_BYTES, (unsigned) RING_SIZE);

    if (pthread_create(&tp, NULL, producer, NULL) != 0) {
        fprintf(stderr, "cannot create the producer thread\n");
        return EXIT_FAILURE;
    }
    if (pthread_create(&tc, NULL, consumer, NULL) != 0) {
        fprintf(stderr, "cannot create the consumer thread\n");
        return EXIT_FAILURE;
    }
    pthread_join(tp, NULL);
    pthread_join(tc, NULL);

    printf("accepted %" PRIu64 "  refused %" PRIu64 "  delivered %" PRIu64
           "  left in ring %u  drops counted %u\n",
           g_accepted, g_refused, g_delivered,
           (unsigned) ring_used(&g_ring), (unsigned) ring_drops(&g_ring));

    int fail = 0;

    if (g_order_fault) {
        printf("ORDER FAULT: a byte arrived out of sequence\n");
        fail = 1;
    }
    if (g_refused != (uint64_t) ring_drops(&g_ring)) {
        printf("POLICY FAULT: the caller counted %" PRIu64
               " refusals and the structure counted %u\n",
               g_refused, (unsigned) ring_drops(&g_ring));
        fail = 1;
    }
    if (g_accepted - g_delivered != (uint64_t) ring_used(&g_ring)) {
        printf("OCCUPANCY FAULT: %" PRIu64 " unread but the ring reports %u\n",
               g_accepted - g_delivered, (unsigned) ring_used(&g_ring));
        fail = 1;
    }

    /* Stated every run, in the output, on purpose. */
    printf("note: this soak cannot detect an ordering fault. The host is "
           "strongly ordered\n");
    printf("      and passes with every barrier removed. See the comment at the "
           "top of this file.\n");

    return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
