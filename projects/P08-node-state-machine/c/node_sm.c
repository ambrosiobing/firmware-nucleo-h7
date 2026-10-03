/* projects/P08-node-state-machine/c/node_sm.c: the table, and a dispatcher that cannot block.
 *
 * The dispatcher is twelve lines and the table is the behaviour. That is the point:
 * a reader who wants to know what the node does reads the table, not the code, and
 * a reviewer can check the table against STATES.md row by row.
 *
 * NO PRIOR ART. The chapter found none worth the name for this, so every line here
 * is the author's. That is unusual in this volume and it is stated rather than
 * implied: there was nothing to copy and therefore nothing to copy wrong.
 *
 * Builds unchanged on the host and on the target. No target headers, no peripheral
 * access, no allocation, nothing that can block.
 */
#include <string.h>

#include "node_sm.h"

#include "payload.h"

/* ------------------------------------------------------------------- actions */

/* Each action is a pure function of the context. None may block, none may fail
 * silently, and none may post an event: the dispatcher returns to the caller and
 * the caller posts what comes next. An action that posted its own event would make
 * the table a graph with hidden edges. */

static void act_nothing(node_ctx_t *ctx) { (void) ctx; }

static void act_start_cycle(node_ctx_t *ctx)
{
    ctx->tx_attempts = 0u;
    ctx->frame_len = 0u;
}

static void act_take_feature(node_ctx_t *ctx)
{
    /* The feature is the sample, scaled. One line, because the point of this
     * project is the transition table rather than the signal processing: P18 is
     * where a real transform lives. Keeping it trivial also keeps the test able to
     * predict the frame exactly, which is what makes the cross-check with P09
     * meaningful. */
    ctx->feature = ctx->pending_sample;
}

static void act_encode(node_ctx_t *ctx)
{
    /* The one place this project touches P09's codec, and the shared acceptance
     * criterion: these bytes must equal what P09's encoder produces for the same
     * input. Calling payload_encode rather than reimplementing it is what makes
     * that true by construction rather than by agreement. */
    const payload_t p = {
        .version  = 1u,
        .flags    = 0u,
        .sequence = ctx->sequence,
        .feature  = ctx->feature,
        .battery  = 40u,
    };
    const size_t n = payload_encode(ctx->frame, sizeof ctx->frame, &p);
    ctx->frame_len = (uint8_t) n;
}

/* tx_attempts counts ATTEMPTS, not failures, and the distinction is the one this
 * project got wrong first. An earlier version incremented it on E_TX_FAIL, which
 * made the counter mean "failures so far" while the guard read it as "attempts so
 * far", and the retry limit was off by one: four attempts were made where three
 * were intended. The test for the limit is what found it.
 *
 * So the attempt is counted where the attempt happens: once when the frame is
 * first handed to the stub, and once more on each retry out of backoff. */
static void act_encode_and_attempt(node_ctx_t *ctx)
{
    act_encode(ctx);
    ctx->tx_attempts = 1u;      /* the frame is handed over now */
}

static void act_another_attempt(node_ctx_t *ctx)
{
    ctx->tx_attempts++;         /* handed over again after the backoff */
}

static void act_cycle_ok(node_ctx_t *ctx)
{
    ctx->cycles_ok++;
    ctx->sequence = (ctx->sequence + 1u) & 0x1FFu;   /* wraps at 512, as P09's field does */
}

static void act_cycle_dropped(node_ctx_t *ctx)
{
    /* Counted, not hidden. A node that silently abandons cycles looks exactly like
     * one that is working. */
    ctx->cycles_dropped++;
    ctx->sequence = (ctx->sequence + 1u) & 0x1FFu;
}

/* -------------------------------------------------------------------- guards */

/* A guard answers one question about the context and nothing else. A guard that
 * changed the context would make the table a lie. */

static bool guard_always(const node_ctx_t *ctx) { (void) ctx; return true; }

static bool guard_may_retry(const node_ctx_t *ctx)
{
    return ctx->tx_attempts < NODE_TX_ATTEMPT_LIMIT;
}

static bool guard_out_of_retries(const node_ctx_t *ctx)
{
    return ctx->tx_attempts >= NODE_TX_ATTEMPT_LIMIT;
}

/* --------------------------------------------------------------------- table */

typedef struct {
    node_state_t from;
    node_event_t event;
    bool       (*guard)(const node_ctx_t *);
    void       (*action)(node_ctx_t *);
    node_state_t to;
    const char  *name;        /* for the coverage report, so a zero names itself */
} node_row_t;

/* Order matters only where two rows share a from-state and an event, which happens
 * once: the two E_TX_FAIL rows are distinguished by their guards, and the retry row
 * comes first. Putting the out-of-retries row first would make the retry row
 * unreachable, and the coverage test is what would catch that. */
static const node_row_t TABLE[] = {
    { S_INIT,    E_TICK,          guard_always,          act_start_cycle,     S_SENSE,   "init: first tick starts a cycle" },
    { S_INIT,    E_FAULT,         guard_always,          act_nothing,         S_FAULT,   "init: fault before anything" },

    { S_IDLE,    E_TICK,          guard_always,          act_start_cycle,     S_SENSE,   "idle: periodic wake" },
    { S_IDLE,    E_BUTTON,        guard_always,          act_start_cycle,     S_SENSE,   "idle: user forced a cycle" },
    { S_IDLE,    E_FAULT,         guard_always,          act_nothing,         S_FAULT,   "idle: fault" },

    { S_SENSE,   E_BLOCK,         guard_always,          act_nothing,         S_FEATURE, "sense: a block arrived" },
    { S_SENSE,   E_TIMEOUT,       guard_always,          act_cycle_dropped,   S_IDLE,    "sense: no block in time" },
    { S_SENSE,   E_FAULT,         guard_always,          act_nothing,         S_FAULT,   "sense: fault" },

    { S_FEATURE, E_FEATURE_DONE,  guard_always,          act_take_feature,    S_ENCODE,  "feature: computed" },
    { S_FEATURE, E_FAULT,         guard_always,          act_nothing,         S_FAULT,   "feature: fault" },

    { S_ENCODE,  E_FRAME_READY,   guard_always,          act_encode_and_attempt, S_TX,   "encode: payload packed, first attempt" },
    { S_ENCODE,  E_FAULT,         guard_always,          act_nothing,         S_FAULT,   "encode: fault" },

    { S_TX,      E_TX_OK,         guard_always,          act_cycle_ok,        S_IDLE,    "tx: accepted" },
    { S_TX,      E_TX_FAIL,       guard_may_retry,       act_nothing,         S_BACKOFF, "tx: failed, will retry" },
    { S_TX,      E_TX_FAIL,       guard_out_of_retries,  act_cycle_dropped,   S_IDLE,    "tx: failed, out of retries" },
    { S_TX,      E_FAULT,         guard_always,          act_nothing,         S_FAULT,   "tx: fault" },

    { S_BACKOFF, E_TIMEOUT,       guard_always,          act_another_attempt, S_TX,      "backoff: elapsed, another attempt" },
    { S_BACKOFF, E_FAULT,         guard_always,          act_nothing,         S_FAULT,   "backoff: fault" },

    /* S_FAULT has no outgoing row on purpose. Only a reset leaves it, and the test
     * asserts that every event posted in S_FAULT is unhandled rather than quietly
     * recovering. A fault state with a way out that nobody designed is how a board
     * comes back to life in an unknown condition. */
};

#define ROW_COUNT (sizeof TABLE / sizeof TABLE[0])

static uint32_t g_hits[ROW_COUNT];
static uint32_t g_unhandled;

/* ---------------------------------------------------------------- dispatcher */

void node_sm_init(node_ctx_t *ctx)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->state = S_INIT;
}

int node_sm_dispatch(node_ctx_t *ctx, node_event_t event)
{
    for (size_t i = 0; i < ROW_COUNT; i++) {
        const node_row_t *r = &TABLE[i];
        if (r->from != ctx->state || r->event != event) {
            continue;
        }
        if (!r->guard(ctx)) {
            continue;
        }
        /* Action before the state change, so an action can read the state it is
         * leaving. Nothing here depends on that today, and fixing the order now
         * costs nothing while discovering it later costs an evening. */
        r->action(ctx);
        ctx->state = r->to;
        g_hits[i]++;
        return (int) i;
    }

    /* No row matched. Not an error: an event that does not apply in this state is
     * ignored deliberately. Counting it is what turns "ignored" into something a
     * reader can check. */
    g_unhandled++;
    return -1;
}

size_t   node_sm_row_count(void)          { return ROW_COUNT; }
uint32_t node_sm_row_hits(size_t row)     { return row < ROW_COUNT ? g_hits[row] : 0u; }
uint32_t node_sm_unhandled(void)          { return g_unhandled; }

const char *node_sm_row_name(size_t row)
{
    return row < ROW_COUNT ? TABLE[row].name : "out of range";
}

void node_sm_reset_hits(void)
{
    memset(g_hits, 0, sizeof g_hits);
    g_unhandled = 0u;
}

const char *node_sm_state_name(node_state_t s)
{
    switch (s) {
    case S_INIT:    return "INIT";
    case S_IDLE:    return "IDLE";
    case S_SENSE:   return "SENSE";
    case S_FEATURE: return "FEATURE";
    case S_ENCODE:  return "ENCODE";
    case S_TX:      return "TX";
    case S_BACKOFF: return "BACKOFF";
    case S_FAULT:   return "FAULT";
    case S_COUNT:   break;
    }
    return "?";
}

const char *node_sm_event_name(node_event_t e)
{
    switch (e) {
    case E_TICK:         return "TICK";
    case E_BLOCK:        return "BLOCK";
    case E_FEATURE_DONE: return "FEATURE_DONE";
    case E_FRAME_READY:  return "FRAME_READY";
    case E_TX_OK:        return "TX_OK";
    case E_TX_FAIL:      return "TX_FAIL";
    case E_TIMEOUT:      return "TIMEOUT";
    case E_BUTTON:       return "BUTTON";
    case E_FAULT:        return "FAULT";
    case E_COUNT:        break;
    }
    return "?";
}
