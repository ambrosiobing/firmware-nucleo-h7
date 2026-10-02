/* c/board/cycles.c: the core's own cycle counter, and why it is checked.
 *
 * The Cortex-M7 carries a Data Watchpoint and Trace unit with a free-running
 * 32 bit counter of core clock cycles, DWT_CYCCNT. This is the one measuring
 * instrument on the part that needs nothing from RM0455: the DWT is
 * architectural, defined by ARM for ARMv7-M, so its registers and their
 * addresses are the same on every Cortex-M7 and ST documents none of them. That
 * makes it available here while the ADC, the timers and the transfer engines are
 * all still waiting on a manual nobody has read.
 *
 * It matters more than its size suggests. Before this, the only time reference
 * in the repository was a host PC's clock reached through a serial port, which
 * on Friday 2 October 2026 was enough to measure the delay loop at eight cycles
 * per iteration and the oscillator at 64.17 MHz, but only by inference and only
 * from the outside. A counter on the die counts the same clock the code runs on.
 *
 * WHY THIS VERIFIES ITSELF RATHER THAN ASSUMING. Enabling CYCCNT takes three
 * writes and any of them can fail to stick:
 *
 *   TRCENA in DEMCR powers the trace block. Without it the DWT registers read
 *   and write as zero and no error is raised anywhere.
 *
 *   DWT_LAR is a CoreSight software lock. On some implementations it must be
 *   written with a magic key before DWT_CTRL accepts anything, and on others it
 *   does not exist and the write is harmless. Which of those applies here is not
 *   worth researching when writing the key costs one instruction either way.
 *
 *   CYCCNTENA in DWT_CTRL starts the counter. On some parts the counter will not
 *   run at all unless a debug probe has already enabled tracing, which means the
 *   same firmware behaves differently depending on whether anybody is watching.
 *
 * So the last step is to read the counter, spin briefly, and read it again. If it
 * did not move, this reports unavailable and every caller falls back. An
 * instrument that cannot be confirmed to be running is not an instrument, and a
 * cycle count of zero looks exactly like a very fast function.
 */
#include "board.h"
#include "stm32h7a3_regs.h"

static bool g_available;

void board_cycles_init(void)
{
    g_available = false;

    DEM_CR   |= (1u << DEM_CR_TRCENA_POS);
    DWT_LAR   = DWT_LAR_KEY;
    DWT_CYCCNT = 0u;
    DWT_CTRL |= (1u << DWT_CTRL_CYCCNTENA_POS);

    /* The check. A few hundred cycles is ample and costs nothing at startup.
     * volatile so the loop survives optimisation, which otherwise deletes it and
     * leaves the two reads adjacent, making a working counter look broken. */
    const uint32_t before = DWT_CYCCNT;
    for (volatile uint32_t i = 0u; i < 64u; i++) {
        __asm volatile ("nop");
    }
    const uint32_t after = DWT_CYCCNT;

    g_available = (after != before);
}

bool board_cycles_available(void)
{
    return g_available;
}

uint32_t board_cycles_now(void)
{
    /* 0 when unavailable rather than a stale register value, so a caller that
     * ignores board_cycles_available() gets a difference of zero rather than
     * noise that looks like a measurement. */
    return g_available ? DWT_CYCCNT : 0u;
}
