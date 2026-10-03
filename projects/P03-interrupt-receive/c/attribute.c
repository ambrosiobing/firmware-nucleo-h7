/* projects/P03-interrupt-receive/c/attribute.c: the arithmetic, and the order of the verdicts.
 *
 * Short, because the decisions are in the header. The one thing worth reading
 * here is the order the verdicts are decided in, because it is load bearing:
 * BRIDGE is tested first, so a step that lost bytes before the peripheral saw
 * them is never also reported as a target finding. A run that measured the
 * bridge says nothing about the target, and a row that claimed both would be
 * the wrong conclusion wearing the right shape.
 */
#include "attribute.h"

#include <stddef.h>

int attr_attribute(const attr_step_t *step, attr_row_t *out)
{
    if (step == NULL || out == NULL) {
        return ATTR_ERR_ARGS;
    }

    const uint32_t sent      = step->sent;
    const uint32_t delivered = step->accepted;
    const uint32_t overrun   = step->overruns;
    const uint32_t dropped   = step->dropped;

    /* Summed in 64 bits on purpose. Three uint32_t counters can exceed a
     * uint32_t between them, and a wrapped sum would make `reached` smaller
     * than it is and turn an over-accounted step into a plausible bridge loss.
     * The Python twin gets this for free and the C has to ask for it, which is
     * the kind of difference writing the same thing twice is for. */
    const uint64_t reached = (uint64_t) delivered + overrun + dropped;

    /* A target that accounts for more bytes than the host sent is not a finding
     * about the link, it is a defect in the measurement: a counter that was not
     * reset between steps, or a host count that is wrong. */
    if (reached > (uint64_t) sent) {
        return ATTR_ERR_OVERACCOUNTED;
    }

    const uint32_t bridge_lost = (uint32_t) ((uint64_t) sent - reached);
    const uint32_t target_lost = overrun + dropped;

    /* One byte in a million, and at least one, so a short run is not judged by
     * a tolerance that rounds to zero. Integer division, no floating point. */
    uint32_t tolerance = sent / ATTR_BRIDGE_TOLERANCE_RECIPROCAL;
    if (tolerance < 1u) {
        tolerance = 1u;
    }

    attr_verdict_t verdict;
    if (bridge_lost > tolerance) {
        verdict = ATTR_BRIDGE;          /* says nothing about the target */
    } else if (target_lost == 0u) {
        verdict = ATTR_PASS;
    } else if (overrun > 0u && dropped == 0u) {
        verdict = ATTR_LATENCY;         /* the handler was too late */
    } else if (dropped > 0u && overrun == 0u) {
        verdict = ATTR_THROUGHPUT;      /* the consumer could not keep up */
    } else {
        verdict = ATTR_BOTH;
    }

    out->rate        = step->rate;
    out->sent        = sent;
    out->delivered   = delivered;
    out->overrun     = overrun;
    out->dropped     = dropped;
    out->bridge_lost = bridge_lost;
    out->target_lost = target_lost;
    out->reached     = (uint32_t) reached;
    out->verdict     = verdict;
    return ATTR_OK;
}

int attr_first_loss(const attr_row_t *rows, uint32_t count)
{
    if (rows == NULL) {
        return -1;
    }

    int best = -1;
    for (uint32_t i = 0; i < count; i++) {
        const attr_verdict_t v = rows[i].verdict;
        if (v != ATTR_LATENCY && v != ATTR_THROUGHPUT && v != ATTR_BOTH) {
            continue;                   /* PASS and BRIDGE are not target loss */
        }
        if (best < 0 || rows[i].rate < rows[(uint32_t) best].rate) {
            best = (int) i;
        }
    }
    return best;
}

const char *attr_verdict_name(attr_verdict_t v)
{
    switch (v) {
    case ATTR_PASS:       return "PASS";
    case ATTR_BRIDGE:     return "BRIDGE";
    case ATTR_LATENCY:    return "LATENCY";
    case ATTR_THROUGHPUT: return "THROUGHPUT";
    case ATTR_BOTH:       return "BOTH";
    }
    return "?";
}
