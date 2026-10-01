/* c/P08/node_sm.h: the node's state machine. Eight states, nine events, one table.
 *
 * No peripherals, no allocation, no blocking. That is what makes it the one piece
 * of application logic in this volume that can be proven completely on a host:
 * every row of the table is driven by a test and the test fails if any row has
 * never been taken.
 *
 * WHAT A ROW MAY CONTAIN, and the restriction is the design. A from-state, an
 * event, an optional guard, an action, a to-state. Nothing else. Putting a timeout
 * value or a retry count in a row is the first step towards a table that describes
 * half the behaviour and a dispatcher that describes the other half, after which
 * neither can be read on its own.
 *
 * THE TRANSMIT STEP IS A STUB AND STAYS ONE. There is no radio on this bench. The
 * stub logs exactly the bytes a radio would have been handed, and the acceptance
 * criterion shared with P09 is that those bytes equal what P09's encoder produces
 * for the same input, byte for byte. A stub that logged something plausible would
 * make the state machine look finished and make P09's codec untested from this
 * side.
 */
#ifndef NODE_SM_H
#define NODE_SM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The states. One sentence each lives in projects/P08-node-state-machine/STATES.md,
 * which was written before this file. A state that needs two sentences is two
 * states. */
typedef enum {
    S_INIT = 0,   /* before the first tick; nothing has been sensed */
    S_IDLE,       /* waiting for the periodic wake */
    S_SENSE,      /* a sample is being taken */
    S_FEATURE,    /* the sample is being reduced to one number */
    S_ENCODE,     /* the number is being packed into a payload */
    S_TX,         /* the payload has been handed to the transmit stub */
    S_BACKOFF,    /* a transmit failed; waiting before trying again */
    S_FAULT,      /* unrecoverable; only a reset leaves this state */
    S_COUNT
} node_state_t;

/* The events. One meaning each. An event with two meanings is two events. */
typedef enum {
    E_TICK = 0,       /* the periodic wake fired */
    E_BLOCK,          /* a block of samples is ready; ctx->pending_sample is set */
    E_FEATURE_DONE,   /* the feature has been computed */
    E_FRAME_READY,    /* the payload has been encoded */
    E_TX_OK,          /* the transmit stub accepted the frame */
    E_TX_FAIL,        /* the transmit stub refused it */
    E_TIMEOUT,        /* the backoff elapsed */
    E_BUTTON,         /* the user asked for a cycle now */
    E_FAULT,          /* something unrecoverable happened */
    E_COUNT
} node_event_t;

/* Everything the machine knows. Flat, fixed size, no pointers to anything it does
 * not own, so a test can construct one on the stack and compare two by value. */
typedef struct {
    node_state_t state;
    uint32_t     tx_attempts;      /* within the current cycle */
    uint32_t     cycles_ok;
    uint32_t     cycles_dropped;   /* gave up after the retry limit */
    int32_t      feature;
    uint32_t     sequence;         /* wraps at 512, as P09's field does */
    int32_t      pending_sample;   /* the seam: a test sets this before E_BLOCK */
    uint8_t      frame[64];
    uint8_t      frame_len;
    bool         tx_should_fail;   /* the seam for exercising BACKOFF and the limit */
} node_ctx_t;

/* The retry limit, here rather than in a row, because a row that carried it would
 * describe policy and transition together and neither would be readable. */
#define NODE_TX_ATTEMPT_LIMIT 3u

void node_sm_init(node_ctx_t *ctx);

/* Post one event. Returns the index of the row taken, or -1 when no row matches,
 * which is not an error: an event that does not apply in the current state is
 * ignored on purpose, and the count of those is itself worth watching. */
int node_sm_dispatch(node_ctx_t *ctx, node_event_t event);

/* Coverage, which is this project's acceptance criterion. The test drives event
 * sequences and then asserts that no row has zero hits. */
size_t      node_sm_row_count(void);
uint32_t    node_sm_row_hits(size_t row);
const char *node_sm_row_name(size_t row);
void        node_sm_reset_hits(void);

/* How many events were posted that no row matched. A rising count here in a real
 * run means something is posting events the design did not expect. */
uint32_t node_sm_unhandled(void);

const char *node_sm_state_name(node_state_t s);
const char *node_sm_event_name(node_event_t e);

#endif /* NODE_SM_H */
