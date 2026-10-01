/* P02/property_test.c: the property test, in C so that it is fast enough to run.
 *
 * Three properties, each catching a different class of fault:
 *
 *   the byte sequence on the far side  catches a masking or wrap fault
 *   the occupancy                      catches an index fault
 *   offered == accepted + refused      catches a policy fault
 *
 * The third is in the book's acceptance criteria and its listing did not
 * actually assert it: it accumulated a drop count and only printed it. Here it
 * is checked twice, against the caller's own tally and against the structure's
 * internal counter, which are different numbers that must agree.
 *
 * WHY THIS IS C AND NOT PYTHON. The first version drove the structure through
 * ctypes and took 127 seconds for four ordering modes, against the book's
 * criterion of under two seconds. A test that takes two minutes is a test
 * somebody switches off. Driven from tests/test_ring.py as a subprocess, with
 * the cheap structural cases staying in Python where direct access to the
 * fields is worth more than speed.
 *
 * WHAT THIS CANNOT FIND. An ordering fault. See soak_threads.c.
 *
 *   ./property_test [steps]
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ring.h"

/* xorshift32, replacing the linear congruential generator the book's listing
 * used. That one's low three bits have a period of eight, and the burst size
 * was taken modulo RING_SIZE + 8, which is 264, which is 8 times 33. By the
 * Chinese remainder theorem the burst sizes therefore carried a period-eight
 * structure, so the test crossed the buffer boundaries far less randomly than it
 * appeared to. Checked before this was written: n % 8 repeats 4, 3, 6, 5, 0, 7,
 * 2, 1 for ever. */
static uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

int main(int argc, char **argv)
{
    unsigned long steps = 200000ul;
    if (argc > 1) {
        steps = strtoul(argv[1], NULL, 10);
    }

    ring_t   r;
    uint32_t s = 0x2026091u;              /* the seed, recorded on purpose */
    uint64_t offered = 0, accepted = 0, delivered = 0, refused = 0;
    uint32_t peak = 0;
    int      fail = 0;

    ring_init(&r);
    printf("ordering: %s\n", ring_barrier_name());
    printf("capacity: %u bytes, steps: %lu\n", (unsigned) RING_SIZE, steps);

    for (unsigned long step = 0; step < steps && !fail; step++) {
        uint32_t n = xs32(&s) % (RING_SIZE + 8u);     /* deliberately over capacity */
        for (uint32_t i = 0; i < n; i++) {
            offered++;
            if (ring_put(&r, (uint8_t) (accepted & 0xFFu))) {
                accepted++;
            } else {
                refused++;
            }
        }

        uint32_t used = ring_used(&r);
        if ((uint64_t) used != accepted - delivered) {
            printf("OCCUPANCY FAULT at step %lu: used %u expected %" PRIu64 "\n",
                   step, used, accepted - delivered);
            fail = 1;
            break;
        }
        if (used > peak) {
            peak = used;
        }

        n = xs32(&s) % (RING_SIZE + 8u);
        for (uint32_t i = 0; i < n; i++) {
            uint8_t b;
            if (!ring_get(&r, &b)) {
                break;
            }
            if (b != (uint8_t) (delivered & 0xFFu)) {
                printf("ORDER FAULT at step %lu: got %u expected %u\n",
                       step, (unsigned) b, (unsigned) (delivered & 0xFFu));
                fail = 1;
                break;
            }
            delivered++;
        }
    }

    if (offered != accepted + refused) {
        printf("POLICY FAULT: offered %" PRIu64 " accepted %" PRIu64
               " refused %" PRIu64 "\n", offered, accepted, refused);
        fail = 1;
    }
    if ((uint64_t) ring_drops(&r) != refused) {
        printf("POLICY FAULT: the structure counted %u refusals, the caller "
               "counted %" PRIu64 "\n", (unsigned) ring_drops(&r), refused);
        fail = 1;
    }
    if (peak > RING_SIZE) {
        printf("OCCUPANCY FAULT: peak %u exceeded the capacity\n", (unsigned) peak);
        fail = 1;
    }

    printf("offered %" PRIu64 "  accepted %" PRIu64 "  refused %" PRIu64
           "  delivered %" PRIu64 "\n", offered, accepted, refused, delivered);
    printf("peak occupancy %u of %u, %.1f bytes accepted per step\n",
           (unsigned) peak, (unsigned) RING_SIZE,
           steps ? (double) accepted / (double) steps : 0.0);
    printf("%s\n", fail ? "FAIL" : "PASS");

    return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
