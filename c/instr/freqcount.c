/* freqcount.c: the gated counter. See freqcount.h for why it exists and what
 * its resolution is.
 *
 * TO BE CONFIRMED against RM0455 and the board manual, and none of it is
 * guessed here: which timer offers external clock mode 2 on a pin that reaches
 * the Zio header, the alternate-function number for that pin, the trigger
 * selection value, and the timer clock for the gate. Until those are read,
 * freqcount_init returns a negative value and the counter reports zero rather
 * than a plausible number.
 */
#include "freqcount.h"

static int ready;

int freqcount_init(void)
{
    ready = 0;
    return -1;   /* unconfirmed; refusing is correct */
}

uint64_t freqcount_measure_mhz(uint32_t gate_ms)
{
    (void) gate_ms;
    return ready ? 0u : 0u;
}

uint64_t freqcount_resolution_mhz(uint32_t gate_ms)
{
    /* One count in the gate. For a 1000 ms gate that is 1 Hz, which is
     * 1000 millihertz. */
    return gate_ms ? (1000000u / gate_ms) : 0u;
}
