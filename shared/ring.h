/* shared/ring.h: single producer, single consumer byte ring.
 *
 * P02 is where this is built and argued. It lives in shared/ rather than in
 * P02/ because P03 fills it from an interrupt, P04 from a transfer engine, P05
 * parses frames out of it and P11 runs a command queue on top of it. It belongs
 * to all of them rather than to whichever project happened to need it first.
 *
 * THE INVARIANT, which is the whole reason this needs no lock:
 *
 *   The consumer may read a slot only after the producer has published a head
 *   that includes it, and the producer may write a slot only after the consumer
 *   has published a tail that releases it.
 *
 * Exactly one context may call ring_put and exactly one other may call
 * ring_get. Two producers are not supported and the structure gives no
 * diagnostic if you try: it is the most common way this fails in the field, and
 * it presents as data that is fine for hours and then wrong once. A queue owned
 * by a kernel is the answer there, which is P20.
 *
 * head and tail are free running counts of bytes ever written and ever read.
 * They are never wrapped; only the array subscript is masked. Unsigned overflow
 * is defined in C, so head - tail stays correct across the wrap of the counters
 * themselves, and no slot is sacrificed to distinguish full from empty. The
 * capacity is therefore the number written below and not one less.
 */
#ifndef RING_H
#define RING_H

#include <stddef.h>
#include <stdint.h>

/* Overridable so the test suite can prove the capacity assertion fires. See
 * tools/check_ring_assert.py, which compiles this with a bad value on purpose
 * and requires the build to fail. */
#ifndef RING_SIZE
#define RING_SIZE 256u
#endif

#define RING_MASK (RING_SIZE - 1u)

typedef struct {
    uint8_t  buf[RING_SIZE];
    uint32_t head;       /* written by the producer only */
    uint32_t tail;       /* written by the consumer only */
    uint32_t drops;      /* producer only: bytes refused because it was full */
} ring_t;

void     ring_init(ring_t *r);
int      ring_put(ring_t *r, uint8_t b);   /* 1 accepted, 0 refused and counted */
int      ring_get(ring_t *r, uint8_t *b);  /* 1 delivered, 0 empty */
uint32_t ring_used(const ring_t *r);
uint32_t ring_drops(const ring_t *r);

/* Which ordering this translation unit was built with, for the status line and
 * for the measurement table. Returns the string form of RING_BARRIER. */
const char *ring_barrier_name(void);

#endif /* RING_H */
