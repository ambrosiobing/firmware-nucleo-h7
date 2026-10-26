/* rxspan.h: the position arithmetic of chapter 4, and the two guards.
 *
 * WHAT THIS IS AND WHY IT IS A SEPARATE FILE. Chapter 4's receive path has no
 * interrupt per byte: a transfer engine writes into a circular buffer and the
 * processor learns where it got to by reading the transfer counter. The whole of
 * the difficult part is working out, from the previous position and the current
 * one, which bytes are new, as at most two spans neither of which crosses the end
 * of the array. That arithmetic needs no peripheral, no board and no clock, so it
 * is written here where a host can prove it and the board links the same object.
 *
 * WHAT IS HERE AND WHAT IS NOT. Here: the span split, the advance, and the two
 * guards of the chapter's step 7. Not here, and needing the board: the counter
 * read itself, the three interrupt handlers that all call one function, and the
 * cache invalidate that the canonical reference leaves out. Those are chapter 4's
 * remaining work and this file does not pretend to cover them.
 *
 * THE GUARD IS NOT A DETECTOR AND THE CHAPTER SAYS SO. If the consumer falls
 * behind far enough, the engine writes over bytes nobody read, and this
 * arithmetic cannot tell a full lap from no movement at all: both arrive as
 * pos == old_pos. There is no overrun flag on this path, unlike chapter 3's.
 * What is offered instead is a bound and a report: how far the position moved in
 * one step, and a count of the steps that moved more than half the buffer.
 */
#ifndef RXSPAN_H
#define RXSPAN_H

#include <stdint.h>

/* At most two, because a circular buffer wraps at most once between two reads
 * of the position, and because a span that crossed the end would deny the
 * consumer the plain pointer and length the chapter promises it. */
#define RXSPAN_MAX 2

/* A floor on sense rather than on correctness. The near-lap guard compares the
 * advance against half the buffer, so a buffer small enough that half of it is
 * two or three bytes makes the guard meaningless rather than wrong. Eight is the
 * smallest size at which both spans of a wrap can be non-trivial. */
#define RXSPAN_MIN_SIZE 8u

typedef struct {
    uint16_t off;              /* offset into the buffer, never past the end */
    uint16_t len;              /* bytes, and off + len is never above size   */
} rxspan_t;

typedef struct {
    uint16_t peak_advance;     /* the largest one-step advance seen          */
    uint32_t near_lap;         /* steps that moved more than half the buffer */
    uint32_t calls;            /* how many times the arithmetic ran          */
    uint32_t deliveries;       /* spans handed out, which is not calls       */
} rxspan_stats_t;

/* The refusals, negative so a caller can tell them from a span count. */
#define RXSPAN_ERR_ARGUMENT (-1)   /* a null out array or null stats         */
#define RXSPAN_ERR_SIZE     (-2)   /* not a power of two, or below the floor */
#define RXSPAN_ERR_POSITION (-3)   /* a position at or past the end          */

/* Returns the number of spans, 0, 1 or 2, having written that many entries into
 * out, or one of the negative refusals above having written none.
 *
 * THE SIZE MUST BE A POWER OF TWO and the refusal for that is load bearing
 * rather than decorative: the advance is computed by masking, exactly as the
 * chapter's step 7 does, and masking with size - 1 is the advance only when the
 * size is a power of two. A buffer of 500 bytes would produce a plausible wrong
 * number rather than an error, which is this volume's least favourite kind of
 * failure.
 *
 * The stats are updated on every call that is not refused, INCLUDING calls that
 * find nothing new, because the count of calls is what the chapter's half and
 * full transfer interrupts exist to keep above a floor. */
int rxspan_next(uint16_t size, uint16_t old_pos, uint16_t pos,
                rxspan_t out[RXSPAN_MAX], rxspan_stats_t *stats);

#endif /* RXSPAN_H */
