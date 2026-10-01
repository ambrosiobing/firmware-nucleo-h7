/* c/P03/faults.c: the two documented failure modes, reproduced on purpose.
 *
 * Both come from the silicon vendor's own community, reported repeatedly over
 * years, with the register-level explanation in the threads. Neither is
 * reproduced from library source here: the call pattern that triggers each is
 * described, the symptom is named, and the repair is written at register level so
 * it can be read and checked. Never present vendor code as reviewed source.
 *
 * Enabled only with -DRX_FAULT=rearm|overrun. A build with no fault flag has
 * neither, and that is the default, because a repository whose default build
 * contains a deliberate defect is a trap for whoever clones it next.
 *
 * WHY REPRODUCE A BUG AT ALL. The repair is only convincing once the symptom has
 * been seen. Both of these are silent in a way that matters: the first loses
 * bytes with no error anywhere, and the second presents as a board that has
 * stopped rather than as a fault. A reader who has watched each happen will
 * recognise them in their own code years later, which is worth more than a
 * paragraph saying they exist.
 */
#include "rx.h"

#if defined(RX_FAULT_REARM)
#  define FAULT_NAME "rearm: the receive call disables its own interrupt"
#elif defined(RX_FAULT_OVERRUN)
#  define FAULT_NAME "overrun: ready without clearing the flag"
#else
#  define FAULT_NAME "none"
#endif

bool rx_fault_injection_enabled(void)
{
#if defined(RX_FAULT_REARM) || defined(RX_FAULT_OVERRUN)
    return true;
#else
    return false;
#endif
}

const char *rx_fault_name(void)
{
    return FAULT_NAME;
}

/* ------------------------------------------------------------------ mode one
 *
 * THE SYMPTOM. Bytes go missing at every rate, in small numbers, with no error
 * flag set and no counter moving. The link looks like it works and the data is
 * quietly incomplete.
 *
 * THE CAUSE, as the threads explain it. The library's interrupt-driven receive
 * call takes a count. When that many bytes have arrived it disables its own
 * receive interrupt and calls a completion callback. Every byte that arrives
 * between that moment and the next call that re-arms the receive is lost, and
 * nothing records it: the peripheral had no interrupt enabled, so no overrun is
 * flagged either.
 *
 * WHY IT IS EASY TO MISS. With a count of one and a slow line it almost never
 * happens, so it survives every test somebody writes at a comfortable rate. It
 * appears under load, which is where the measurement in this project looks.
 *
 * THE REPAIR, which is what rx_ring.c does. Never disable the receive interrupt.
 * Leave it enabled for the life of the program, take one byte per interrupt, and
 * put it somewhere that cannot block. The count belongs to the application, not
 * to the peripheral, and P02's ring is where it goes. There is then no window to
 * lose a byte in, because there is no moment when the interrupt is off.
 *
 * To reproduce: build with -DRX_FAULT=rearm, which makes the handler disable
 * its own interrupt after RX_FAULT_REARM_COUNT bytes and re-arm it from the main
 * loop, which is exactly the pattern the library uses.
 */
#define RX_FAULT_REARM_COUNT 16u

/* ------------------------------------------------------------------ mode two
 *
 * THE SYMPTOM. The board stops responding. Not a fault, not a reset, no output:
 * it simply never returns to the main loop again. A debugger attached afterwards
 * finds the processor inside the receive handler.
 *
 * THE CAUSE. An overrun sets a flag that is separate from the data register. The
 * library's error path returns the peripheral to its ready state without
 * clearing that flag, so the interrupt condition is still true the instant the
 * handler returns, and the handler is entered again immediately, for ever. The
 * main loop never runs, which is why it looks like a stop rather than an error.
 *
 * WHY IT IS WORSE THAN LOSING A BYTE. Losing a byte is a data problem. This is a
 * liveness problem: the watchdog is the only thing that can save the board, and
 * a watchdog fed from a timer interrupt will happily keep feeding while the main
 * loop is starved, which is P12's subject.
 *
 * THE REPAIR. Clear the flag explicitly through the interrupt flag clear
 * register, in the handler, on the error path, before returning. On this family
 * the clear is a write to a dedicated register rather than a read of the status
 * register, and that difference is exactly what material written for an older
 * family gets wrong. The sequence is TO BE CONFIRMED against RM0455 because the
 * flag clearing procedure differs between families, and the most widely read
 * worked example of this problem is written for an older one.
 *
 * To reproduce: build with -DRX_FAULT=overrun, which makes the handler take the
 * error path without the clear.
 */
