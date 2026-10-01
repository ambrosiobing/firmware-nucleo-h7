/* c/board/system.c: the clock tree, and what it refuses to do.
 *
 * SystemInit runs from Reset_Handler before main and before any constructor.
 * On this part it has one job it can do and one it cannot.
 *
 * It CAN enable the floating point unit, which is architectural and needs no
 * part-specific value.
 *
 * It CANNOT raise the core to 280 MHz, because that needs three things from
 * RM0455 that this repository has not read: the PLL register fields, the flash
 * access latency for the target frequency, and the voltage scaling that must be
 * set before the frequency is raised. Each of those gets the part wrong in a
 * different way. Too little flash latency produces a part that executes garbage
 * once the clock rises. Wrong voltage scaling produces one that works at room
 * temperature and fails warm. The wrong PLL divider produces a part that runs
 * at a plausible wrong speed, which is the worst outcome of the three because
 * everything appears to work and every measured interval is wrong by a constant
 * factor.
 *
 * So the part stays on its reset clock and board_clock_status() says so. An LED
 * still blinks, which is what P01 calls first light, and nothing times anything
 * until the rate is established.
 */
#include "board.h"
#include "stm32h7a3_regs.h"

static board_status_t g_clock_status = BOARD_ERR_CLOCK_UNCONFIRMED;
static uint32_t       g_core_hz;        /* 0 until established */

void SystemInit(void)
{
    /* The floating point unit. Architectural, both coprocessor fields to full
     * access. Done here rather than in main because a constructor may use a
     * float, and constructors run before main. */
    SCB_CPACR |= (3u << 20) | (3u << 22);
    __asm volatile ("dsb");
    __asm volatile ("isb");

#ifdef BOARD_REGS_CONFIRMED
    /* The order below is not negotiable and is the part of this that RM0455
     * governs. Written as the steps rather than as code, because writing the
     * code from recollection of a sibling part is exactly the failure this
     * volume exists to avoid:
     *
     *   1. Raise the voltage scaling to the level the target frequency needs,
     *      and wait for the regulator to report ready.
     *   2. Set the flash access latency for the target frequency, before the
     *      frequency changes, and read the register back to confirm it took.
     *   3. Enable the external oscillator in bypass mode, 8 MHz from the
     *      on-board debugger, and wait for it to be ready.
     *   4. Configure the PLL: 8 / 2, times 140, divided by 2, giving 280 MHz.
     *   5. Enable the PLL, wait for lock, then switch the system clock to it.
     *   6. Read the clock configuration register back and derive the frequency
     *      from what it actually says rather than from what was intended.
     *
     * Step 6 is the one most implementations omit, and it is the one that turns
     * a guessed clock into a measured one. Until it is written, the two lines
     * below stay as they are.
     */
    g_clock_status = BOARD_ERR_CLOCK_UNCONFIRMED;
    g_core_hz = 0u;
#else
    /* Nothing is confirmed, so nothing is claimed. */
    g_clock_status = BOARD_ERR_CLOCK_UNCONFIRMED;
    g_core_hz = 0u;
#endif
}

board_status_t board_clock_status(void)
{
    return g_clock_status;
}

uint32_t board_core_hz(void)
{
    return g_core_hz;
}

bool board_delay_ms(uint32_t ms)
{
    /* With no established core frequency this cannot be a time, and it says so
     * by returning false. It still delays, because a blinking LED is more useful
     * than a refusal at this point in the bring-up, but the return value is
     * there so that nothing times anything with it by accident.
     *
     * The loop count assumes the unconfirmed internal oscillator frequency and
     * roughly four cycles per iteration. Both are approximations and that is
     * the whole reason this returns false. */
    const uint32_t hz = g_core_hz ? g_core_hz : HSI_HZ_UNCONFIRMED;
    const uint32_t iterations = (hz / 4000u) * ms;

    for (volatile uint32_t i = 0; i < iterations; i++) {
        __asm volatile ("nop");
    }
    return g_core_hz != 0u;
}
