/* zonetrack.h: chapter 15's hysteresis, the part that needs no sensor.
 *
 * WHAT THIS IS. The sensor reports an eight by eight grid several times a
 * second and the node sends a host one status byte. The thing that makes that
 * byte worth anything is not the reduction, which is arithmetic, but the
 * hysteresis: what stops the output changing state every time a person shifts
 * their weight. That is a state machine over a sequence of distances, and a
 * state machine over a sequence needs no sensor, no bus and no board to prove.
 *
 * WHERE THE SPECIFICATION COMES FROM, and it is two sources that disagree in
 * coverage. Chapter 15's step 9 gives 800 mm to enter, 950 mm to leave and three
 * agreeing frames out of five. Figure 15.4 gives the same two numbers, the same
 * confirmation count, AND a third threshold of 1500 mm that the step 9 sketch
 * omits, as the gate between clear and approaching and between leaving and
 * clear. **The figure is the more complete of the two** and this file follows
 * it:
 *
 *   clear       -> approaching   under 1500 mm, immediately
 *   approaching -> present       under 800 mm, three frames of five
 *   present     -> leaving       over 950 mm, three frames of five
 *   leaving     -> clear         over 1500 mm, immediately
 *
 * THE THRESHOLDS ARE PARAMETERS AND NOT CONSTANTS HERE, because the chapter
 * says in the figure's own note that the numbers are "a starting point measured
 * against a real target rather than constants to be trusted". Baking them in
 * would make this file disagree with the chapter the first time a measurement
 * moved one. The caller passes them and the oracle varies them.
 *
 * WHAT IS DELIBERATELY NOT HERE. The validity filter of step 8 needs a sigma
 * limit, a minimum distance and the driver's set of trusted status codes, and
 * the chapter gives none of the three. The reduction needs an occupancy bucket
 * mapping and the status byte needs a confidence field, and the chapter defines
 * neither. **Inventing six constants to fill a struct would be fiction**, and
 * they are bench decisions by the chapter's own account. They arrive when the
 * sensor does.
 */
#ifndef ZONETRACK_H
#define ZONETRACK_H

#include <stdbool.h>
#include <stdint.h>

/* The four states, numbered as chapter 15's host decoder indexes them:
 * ["clear", "approaching", "present", "leaving"][(b >> 5) & 3]. The numbering
 * is part of the wire format and not an internal detail. */
typedef enum {
    ZONETRACK_CLEAR = 0,
    ZONETRACK_APPROACHING = 1,
    ZONETRACK_PRESENT = 2,
    ZONETRACK_LEAVING = 3
} zonetrack_state_t;

/* The policy, passed rather than compiled in. See the header note above. */
typedef struct {
    uint16_t far_mm;      /* the gate between clear and approaching       */
    uint16_t enter_mm;    /* approaching to present, with confirmation    */
    uint16_t leave_mm;    /* present to leaving, with confirmation        */
    uint8_t  confirm_n;   /* agreeing frames needed                       */
    uint8_t  confirm_m;   /* out of how many, the window                  */
} zonetrack_policy_t;

/* The window is a bitmask, so the number of frames it can hold is the number of
 * bits in it. Sixteen is more than chapter 15 needs and the refusal below is
 * what keeps a caller from asking for more. */
#define ZONETRACK_WINDOW_MAX 16u

typedef struct {
    zonetrack_state_t state;
    uint16_t votes;       /* one bit per frame in the window, newest at bit 0 */
    uint8_t  seen;        /* frames in the window, saturating at confirm_m    */
    uint32_t frames;      /* every frame this tracker has been given          */
    uint32_t changes;     /* state changes, which is the figure a policy is
                           * judged by: hundreds in an hour means the
                           * thresholds are too close together               */
    bool     ready;       /* a full window has been seen at least once        */
} zonetrack_t;

/* The refusals, negative so they cannot be confused with a state. */
#define ZONETRACK_ERR_ARGUMENT (-1)  /* a null tracker or null policy          */
#define ZONETRACK_ERR_POLICY   (-2)  /* thresholds out of order, or a window
                                      * of zero, above the maximum, or with
                                      * confirm_n above confirm_m             */

/* Start a tracker in the clear state with an empty window. Returns 0, or
 * ZONETRACK_ERR_ARGUMENT. The policy is validated by zonetrack_update rather
 * than here, because a tracker outlives the call that made it. */
int zonetrack_init(zonetrack_t *t);

/* Feed one frame's nearest valid distance in millimetres and get the state
 * after it, or a negative refusal. A frame with no valid zone at all is passed
 * as ZONETRACK_NO_TARGET, which is treated as further away than any threshold.
 *
 * THE WINDOW IS CLEARED ON EVERY STATE CHANGE, which is a decision this file
 * makes and the chapter does not: votes gathered while approaching say nothing
 * about whether to leave, and carrying them over would let a target that
 * crossed 800 mm three times on the way in satisfy the exit test on the way
 * out. */
#define ZONETRACK_NO_TARGET 0xFFFFu

int zonetrack_update(zonetrack_t *t, const zonetrack_policy_t *p,
                     uint16_t nearest_mm);

/* The name of a state, for a console line and for a test's failure message.
 * Never NULL: an out of range value returns "invalid". */
const char *zonetrack_state_name(int state);

#endif /* ZONETRACK_H */
