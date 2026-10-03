/* projects/P03-interrupt-receive/c/attribute.h: every sent byte goes to exactly one place.
 *
 * THE HALF OF P03 THAT CAN BE PROVEN. The receive path in rx_ring.c needs a
 * peripheral and refuses until RM0455 is read. The attribution does not: it is
 * arithmetic over counters a run recorded, and it is the half where a wrong
 * answer publishes a wrong conclusion rather than merely failing to work.
 *
 * WHY IT IS WORTH FOUR IMPLEMENTATIONS. The easiest way to publish a wrong
 * result about a serial link is to report one figure called "bytes lost". The
 * common finding, that a link fails above some rate, is usually a statement
 * about the bridge between the host and the board and not about the board. A
 * number that cannot tell those apart is worse than no number, because somebody
 * will quote it.
 *
 * So every byte the host sent is attributed to exactly one of four places:
 *
 *   delivered     the target handed it to the application
 *   overrun       the peripheral had it and the handler was too late
 *   dropped       the handler read it and the ring was full
 *   bridge_lost   it never reached the peripheral at all
 *
 * The first three are target counters. The fourth is a subtraction and it is the
 * one that decides whether a run means anything: if bytes went missing before
 * the peripheral saw them, the run measured the bridge and the target's limit is
 * still unknown.
 *
 * THE TWO KINDS OF TARGET LOSS ARE NEVER SUMMED. An overrun is a latency
 * failure, fixed by shortening the handler or raising its priority. A drop is a
 * throughput failure, fixed by a larger ring or a faster consumer. One figure
 * called "lost" points at neither.
 *
 * This file is host arithmetic and compiles unchanged for the target, which is
 * deliberate: the same attribution could run on the board for a self-test,
 * though nothing does that today.
 */
#ifndef ATTRIBUTE_H
#define ATTRIBUTE_H

#include <stdint.h>

/* A handful of bytes astray in a run of millions is the bridge's buffering
 * rather than a finding. Above this fraction the run is about the bridge and
 * says nothing about the target.
 *
 * Expressed as a reciprocal so there is no floating point in the comparison:
 * one byte in a million. The Python twin computes the same thing as
 * int(sent * 1e-6), and the two were swept against each other across the 32-bit
 * range rather than assumed equal. They never differ, because the multiply
 * rounds back onto the exact integer, and the sweep is recorded in
 * python/tests/test_attribute_parity.py so the claim is checked rather than
 * remembered. */
#define ATTR_BRIDGE_TOLERANCE_RECIPROCAL 1000000u

/* What a step was, in the order the verdicts are decided. */
typedef enum {
    ATTR_PASS       = 0,  /* the target lost nothing */
    ATTR_BRIDGE     = 1,  /* bytes went missing before the peripheral saw them */
    ATTR_LATENCY    = 2,  /* overruns only: the handler was too late */
    ATTR_THROUGHPUT = 3,  /* drops only: the consumer could not keep up */
    ATTR_BOTH       = 4,  /* both kinds, which are still not summed */
} attr_verdict_t;

/* Why an attribution was refused. Refusing matters as much as attributing: a
 * target that accounts for more bytes than the host sent is a defect in the
 * measurement, not a finding about the link, and reporting a negative bridge
 * loss or clamping it to zero would turn that defect into a plausible row. */
typedef enum {
    ATTR_OK                  =  0,
    ATTR_ERR_OVERACCOUNTED   = -1,  /* reached > sent: a counter was not reset */
    ATTR_ERR_ARGS            = -2,  /* a null pointer */
} attr_status_t;

/* What a run recorded for one step. Unsigned, because a negative counter is not
 * a representable state here: the Python twin has to check for one because its
 * integers are signed, and that difference is itself a finding the parity test
 * records rather than papers over. */
typedef struct {
    uint32_t rate;        /* baud, carried through for the table */
    uint32_t sent;        /* what the host sent */
    uint32_t accepted;    /* what the path handed to the application */
    uint32_t overruns;    /* the peripheral had a byte we did not read in time */
    uint32_t dropped;     /* the ring was full */
} attr_step_t;

typedef struct {
    uint32_t rate;
    uint32_t sent;
    uint32_t delivered;
    uint32_t overrun;
    uint32_t dropped;
    uint32_t bridge_lost;
    uint32_t target_lost;
    uint32_t reached;
    attr_verdict_t verdict;
} attr_row_t;

/* Attribute one step. Returns ATTR_OK and fills *out, or a negative
 * attr_status_t and leaves *out untouched. */
int attr_attribute(const attr_step_t *step, attr_row_t *out);

/* The lowest rate at which the target itself lost a byte, or -1 when none did.
 *
 * Rows with a BRIDGE verdict are skipped rather than counted as a target
 * failure, because that is exactly the confusion the whole file exists to
 * prevent. A ramp whose every row is BRIDGE has found nothing about the target,
 * and returning -1 says so rather than naming the lowest bridge failure.
 *
 * Returns the index into rows, not the rate, so a caller can print the whole
 * row. The rows need not be sorted: the lowest rate is found, not the first.
 */
int attr_first_loss(const attr_row_t *rows, uint32_t count);

const char *attr_verdict_name(attr_verdict_t v);

#endif /* ATTRIBUTE_H */
