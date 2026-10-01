/* c/ring/ring.c: the structure. Short, because the decisions came first.
 *
 * No target headers are included, so this file compiles unchanged for the host
 * and for the Cortex-M7. That is what makes the test in tests/test_ring.py
 * honest, and it is the habit every later project depends on.
 */
#include "ring.h"

#include "barrier.h"

/* The capacity constraint is an assertion rather than a comment, so an edit to
 * an awkward number stops the build instead of producing a structure whose mask
 * silently no longer matches its size. tools/check_ring_assert.py proves this
 * fires rather than leaving it to be verified by hand once and then trusted. */
_Static_assert((RING_SIZE & RING_MASK) == 0u, "RING_SIZE must be a power of two");
_Static_assert(RING_SIZE >= 2u, "a capacity of one leaves no room to be full");
_Static_assert(RING_SIZE <= 0x80000000u, "capacity must stay under half the counter");

void ring_init(ring_t *r)
{
    r->head = 0u;
    r->tail = 0u;
    r->drops = 0u;
}

/* Producer context only.
 *
 * The stale read here is safe in one direction only, and that is the argument.
 * The producer may read a tail older than the truth, and the consequence is
 * that it believes the buffer fuller than it is and refuses a byte it could
 * have taken. That error is conservative. It cannot produce a write over a slot
 * the consumer has not finished reading, which is the only outcome that would
 * be a fault.
 */
int ring_put(ring_t *r, uint8_t b)
{
    uint32_t h = r->head;                     /* mine: a plain read */
    uint32_t t = RING_LOAD_OTHER(&r->tail);   /* theirs */
    RING_ACQUIRE();                           /* their tail, then my write */

    if ((uint32_t) (h - t) >= RING_SIZE) {
        r->drops++;                           /* policy, counted and not hidden */
        return 0;
    }

    r->buf[h & RING_MASK] = b;                /* the data */
    RING_RELEASE();                           /* data visible before the index */
    RING_STORE_MINE(&r->head, h + 1u);        /* publish */
    return 1;
}

/* Consumer context only.
 *
 * Mirror of the above. The consumer may read a head older than the truth and
 * will then believe the buffer emptier than it is and return early. Also
 * conservative, and also incapable of reading a slot that was never written.
 */
int ring_get(ring_t *r, uint8_t *b)
{
    uint32_t t = r->tail;                     /* mine */
    uint32_t h = RING_LOAD_OTHER(&r->head);   /* theirs */
    RING_ACQUIRE();                           /* their head, then my read */

    if (h == t) {
        return 0;
    }

    *b = r->buf[t & RING_MASK];
    RING_RELEASE();                           /* read done before the release */
    RING_STORE_MINE(&r->tail, t + 1u);
    return 1;
}

/* Correct across the wrap of the counters, because unsigned overflow is defined.
 *
 * Read this from the consumer. From the producer the value is conservative in
 * one direction only, and code that treats it as exact will eventually act on a
 * stale number. */
uint32_t ring_used(const ring_t *r)
{
    return (uint32_t) (r->head - r->tail);
}

uint32_t ring_drops(const ring_t *r)
{
    return r->drops;
}

const char *ring_barrier_name(void)
{
    return RING_BARRIER_NAME;
}
