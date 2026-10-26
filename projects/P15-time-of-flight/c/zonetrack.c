/* zonetrack.c: the four states of chapter 15's figure 15.4.
 *
 * Every intermediate is unsigned 32 bit and every narrowing is an explicit
 * cast, because the host build compiles this with -Wconversion and
 * -Wsign-conversion as errors.
 */
#include "zonetrack.h"

#include <stddef.h>

static bool policy_sane(const zonetrack_policy_t *p)
{
    if (p->confirm_m == 0u || p->confirm_m > ZONETRACK_WINDOW_MAX) {
        return false;
    }
    if (p->confirm_n == 0u || p->confirm_n > p->confirm_m) {
        return false;
    }
    /* enter is nearer than leave, and leave is nearer than far. A policy with
     * leave below enter has no hysteresis at all: the two thresholds would
     * cross and the state would chatter at the crossing, which is the single
     * thing this whole file exists to prevent. */
    if (!(p->enter_mm < p->leave_mm && p->leave_mm < p->far_mm)) {
        return false;
    }
    return true;
}

int zonetrack_init(zonetrack_t *t)
{
    if (t == NULL) {
        return ZONETRACK_ERR_ARGUMENT;
    }
    t->state = ZONETRACK_CLEAR;
    t->votes = 0u;
    t->seen = 0u;
    t->frames = 0u;
    t->changes = 0u;
    t->ready = false;
    return 0;
}

/* Push one vote into the window and report how many of the last confirm_m
 * frames voted yes. The window holds confirm_m bits; anything older falls off
 * the top and is not counted. */
static uint32_t push_vote(zonetrack_t *t, const zonetrack_policy_t *p, bool yes)
{
    const uint32_t keep = (p->confirm_m >= 32u)
                        ? 0xFFFFFFFFu
                        : ((1u << p->confirm_m) - 1u);

    uint32_t window = (((uint32_t) t->votes << 1) | (yes ? 1u : 0u)) & keep;
    t->votes = (uint16_t) window;

    if (t->seen < p->confirm_m) {
        t->seen++;
        if (t->seen == p->confirm_m) {
            t->ready = true;
        }
    }

    uint32_t count = 0u;
    for (uint32_t bit = 0u; bit < (uint32_t) p->confirm_m; bit++) {
        if ((window & (1u << bit)) != 0u) {
            count++;
        }
    }
    return count;
}

static void enter(zonetrack_t *t, zonetrack_state_t next)
{
    if (next != t->state) {
        t->state = next;
        t->changes++;
    }
    /* Cleared on every change, including a change into the same state, which
     * cannot happen here but would be the quiet kind of bug if it could. The
     * header says why votes do not carry across a transition. */
    t->votes = 0u;
    t->seen = 0u;
}

int zonetrack_update(zonetrack_t *t, const zonetrack_policy_t *p,
                     uint16_t nearest_mm)
{
    if (t == NULL || p == NULL) {
        return ZONETRACK_ERR_ARGUMENT;
    }
    if (!policy_sane(p)) {
        return ZONETRACK_ERR_POLICY;
    }

    t->frames++;

    switch (t->state) {
    case ZONETRACK_CLEAR:
        /* Immediate, with no confirmation: the figure draws no count on this
         * edge, and a node that waited three frames to notice something is in
         * the room at all would be slower than the sensor. */
        if (nearest_mm < p->far_mm) {
            enter(t, ZONETRACK_APPROACHING);
        }
        break;

    case ZONETRACK_APPROACHING:
        if (push_vote(t, p, nearest_mm < p->enter_mm)
            >= (uint32_t) p->confirm_n) {
            enter(t, ZONETRACK_PRESENT);
        } else if (nearest_mm >= p->far_mm) {
            /* Went away again without ever confirming. The figure has no edge
             * for this, and leaving the node in approaching for ever after a
             * target walks out of range would be worse than the alternative,
             * so it returns to clear. This is a decision, recorded in the
             * project's README with the other two. */
            enter(t, ZONETRACK_CLEAR);
        }
        break;

    case ZONETRACK_PRESENT:
        if (push_vote(t, p, nearest_mm > p->leave_mm)
            >= (uint32_t) p->confirm_n) {
            enter(t, ZONETRACK_LEAVING);
        }
        break;

    case ZONETRACK_LEAVING:
        if (nearest_mm > p->far_mm) {
            enter(t, ZONETRACK_CLEAR);
        } else if (nearest_mm < p->enter_mm) {
            /* Came back. The figure has no edge for this either, and a target
             * that returns while the node is still calling it leaving should
             * be present again rather than waiting to pass 1500 mm first. */
            enter(t, ZONETRACK_PRESENT);
        }
        break;

    default:
        return ZONETRACK_ERR_ARGUMENT;
    }

    return (int) t->state;
}

const char *zonetrack_state_name(int state)
{
    switch (state) {
    case ZONETRACK_CLEAR:       return "clear";
    case ZONETRACK_APPROACHING: return "approaching";
    case ZONETRACK_PRESENT:     return "present";
    case ZONETRACK_LEAVING:     return "leaving";
    default:                    return "invalid";
    }
}
