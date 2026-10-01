/* cyccnt.h: the cheapest instrument on the board.
 *
 * Two backends behind one interface. The core's own cycle counter when it
 * works, a free running 32-bit timer when it does not. Whether the cycle
 * counter runs with no debugger attached is item 25 on the confirm list, so
 * cyccnt_init does not assume: it enables the counter, checks that it moved,
 * and says which backend it ended up with.
 *
 * The two backends differ in one way that matters: the cycle counter counts
 * core clocks and the timer counts timer clocks. On this board those are the
 * same number only because the clock tree was configured that way, so the tick
 * rate is stored next to the reading rather than assumed.
 */
#ifndef CYCCNT_H
#define CYCCNT_H

#include <stdint.h>

typedef enum {
    CYCCNT_NONE = 0,
    CYCCNT_DWT,      /* the core's trace counter */
    CYCCNT_TIMER     /* a free running 32-bit timer, the fallback */
} cyccnt_backend_t;

/* Returns the backend actually in use. CYCCNT_NONE means neither worked and
 * every timing figure in this run must be reported as not measured. */
cyccnt_backend_t cyccnt_init(void);

uint32_t cyccnt_now(void);

/* Ticks per second for whichever backend is in use. Store this beside any
 * reading; a tick count without its rate is not a time. */
uint32_t cyccnt_tick_hz(void);

const char *cyccnt_backend_name(void);

#endif /* CYCCNT_H */
