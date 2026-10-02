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
static uint32_t       g_pclk1_hz;       /* 0 until established */

/* These are statics written from SystemInit, which is only safe because
 * Reset_Handler copies .data and zeroes .bss BEFORE calling it. The comment at
 * that call site names this exact hazard and says it stays harmless until
 * SystemInit gains a static variable. It just did. If the order in startup.c is
 * ever rearranged, the symptom will be a frequency of zero on a part whose
 * registers are configured perfectly well, and it will be invisible in a
 * diff. */

#ifdef BOARD_REGS_CONFIRMED

/* The AHB and CPU prescaler fields share one four-bit encoding. Only the three
 * ratios ST's device header names are decoded; everything else refuses, because
 * the remaining ratios are not evenly spaced and writing them from recollection
 * of how this family usually encodes them is how a clock ends up wrong by a
 * factor of two with every register apparently correct. */
static uint32_t ahb_cpu_divider(uint32_t field)
{
    switch (field) {
    case RCC_AHBPRE_DIV1: return 1u;
    case RCC_AHBPRE_DIV2: return 2u;
    case RCC_AHBPRE_DIV4: return 4u;
    default:              return 0u;
    }
}

static uint32_t apb_divider(uint32_t field)
{
    switch (field) {
    case RCC_APBPRE_DIV1: return 1u;
    case RCC_APBPRE_DIV2: return 2u;
    case RCC_APBPRE_DIV4: return 4u;
    default:              return 0u;
    }
}

/* sys_ck, in hertz, or 0 when this code cannot say.
 *
 * HSI is decoded; the other three sources refuse, each for its own reason. CSI
 * has a nominal this repository has not sourced. HSE depends on what the board
 * feeds in, which is the debugger's 8 MHz here but is a board fact rather than a
 * register fact. PLL1 would need its own divider chain decoded from four more
 * registers, which is a later chapter, and guessing it would produce exactly the
 * plausible wrong frequency this whole design refuses. */
static uint32_t system_source_hz(void)
{
    const uint32_t cr  = RCC_CR;
    const uint32_t sws = (RCC_CFGR & RCC_CFGR_SWS_MSK) >> RCC_CFGR_SWS_POS;

    if (sws != RCC_SWS_HSI) {
        return 0u;
    }

    /* HSIDIV is two bits and the header names all four values: _1, _2, _4 and _8
     * at field values 0 to 3. So the divisor is 1 << field and the frequency is a
     * right shift, which is exact rather than a rounded division. */
    const uint32_t d = (cr & RCC_CR_HSIDIV_MSK) >> RCC_CR_HSIDIV_POS;
    return HSI_HZ_NOMINAL >> d;
}

/* Decode rather than assume, so this reports the truth on the reset clock now and
 * on the 280 MHz tree once that is written, with no second code path. */
static void clock_establish(void)
{
    const uint32_t sys = system_source_hz();
    if (sys == 0u) {
        g_clock_status = BOARD_ERR_CLOCK_UNCONFIRMED;
        g_core_hz = 0u;
        g_pclk1_hz = 0u;
        return;
    }

    const uint32_t cfg1 = RCC_CDCFGR1;
    const uint32_t cpu_div =
        ahb_cpu_divider((cfg1 & RCC_CDCFGR1_CDCPRE_MSK) >> RCC_CDCFGR1_CDCPRE_POS);
    const uint32_t ahb_div =
        ahb_cpu_divider((cfg1 & RCC_CDCFGR1_HPRE_MSK) >> RCC_CDCFGR1_HPRE_POS);
    const uint32_t apb1_div =
        apb_divider((RCC_CDCFGR2 & RCC_CDCFGR2_CDPPRE1_MSK) >> RCC_CDCFGR2_CDPPRE1_POS);

    if (cpu_div == 0u || ahb_div == 0u || apb1_div == 0u) {
        /* A ratio outside the decoded set. Refusing is right: reporting the
         * undivided frequency here would be wrong by that very ratio. */
        g_clock_status = BOARD_ERR_CLOCK_UNCONFIRMED;
        g_core_hz = 0u;
        g_pclk1_hz = 0u;
        return;
    }

    /* The order of the chain on this part: sys_ck divided by CDCPRE gives the
     * core clock, the core clock divided by HPRE gives the AHB buses, and the
     * AHB clock divided by CDPPRE1 gives APB1. Getting that order wrong matters
     * only when the ratios differ from one, which is why it has to be right now
     * rather than when it starts to show. */
    g_core_hz  = sys / cpu_div;
    g_pclk1_hz = (g_core_hz / ahb_div) / apb1_div;

    g_clock_status = (g_core_hz == CORE_HZ_TARGET)
                   ? BOARD_OK
                   : BOARD_CLOCK_AT_RESET_SPEED;
}

#endif  /* BOARD_REGS_CONFIRMED */

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
    /* The 280 MHz tree is still not configured, and the steps above are still
     * what it would take. What changed on Friday 2 October 2026 is that not
     * configuring it no longer means not knowing the frequency. The registers
     * state what the clock is, so they are read and decoded, and the status
     * distinguishes "running at the reset speed, which is this many hertz" from
     * "cannot say". Those were the same value before and should not have been. */
    clock_establish();
#else
    /* Nothing is confirmed, so nothing is claimed. */
    g_clock_status = BOARD_ERR_CLOCK_UNCONFIRMED;
    g_core_hz = 0u;
    g_pclk1_hz = 0u;
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

uint32_t board_pclk1_hz(void)
{
    return g_pclk1_hz;
}

bool board_delay_ms(uint32_t ms)
{
    /* This still returns false, and the reason it returns false has changed.
     *
     * There were two approximations here. The frequency was one, and it is now
     * established: g_core_hz is decoded from the RCC registers and is a
     * datasheet nominal with about one percent on it rather than an unknown.
     *
     * The other has been measured, and the measurement is why this still refuses
     * rather than why it could stop.
     *
     * The loop count assumed roughly four cycles per iteration. On Friday
     * 2 October 2026 that was measured on the board and it is eight. Exactly
     * eight: P01 prints a note every twentieth blink cycle, three consecutive
     * note to note intervals came out at 19966, 19966 and 19963 ms, a spread of
     * 0.015 per cent, and dividing gives 7.99 cycles per iteration. So the blink
     * had been running at 998 ms where the code intended 500, wrong by a factor
     * of two, and the divisor below is corrected from 4000 to 8000.
     *
     * Eight is believable from the loop itself: the counter is declared volatile,
     * which forces a load and a store to memory every iteration instead of
     * keeping it in a register, roughly doubling what the loop would otherwise
     * cost.
     *
     * That same measurement incidentally weighed the clock. Cycles per iteration
     * must be a whole number, so 7.99 cannot be the cycle count; it is the
     * divisor being slightly off, and the divisor is the frequency. Seven cycles
     * would imply 56.1 MHz and nine would imply 72.1 MHz, neither of them
     * anywhere near. Eight implies 64.11 MHz, 0.18 per cent above the 64.00 MHz
     * nominal and well inside the oscillator's specified tolerance. That is the
     * first figure for this part's clock that is a measurement rather than a
     * datasheet value, and its reference is a host PC's clock over three twenty
     * second intervals, which is good to far better than a tenth of a per cent
     * and is not traceable to anything.
     *
     * AND IT STILL RETURNS FALSE. Eight cycles is eight cycles for this compiler
     * at this optimisation level with this code in flash and this clock. Change
     * the optimisation level, move the loop, enable the instruction cache, or
     * raise the clock so the flash wait states bite differently, and it moves,
     * and nothing in the build checks that it has not. A figure that holds only
     * under conditions nobody verifies is not one to hand a caller as
     * trustworthy. P06 replaces the estimate with a counted reference, which is
     * what makes a delay a time.
     *
     * Worth stating plainly because the temptation runs the other way: settling
     * the clock made one of two approximations go away, and it would have been
     * easy to let the return value become true on the strength of that. A caller
     * reading true would then believe a figure that is still wrong by whatever
     * the real cycles per iteration turns out to be. P06 is where a delay gets a
     * counted reference instead of a guessed one. */
    const uint32_t hz = g_core_hz ? g_core_hz : HSI_HZ_NOMINAL;
    /* 8000, not 4000: eight measured cycles per iteration and a thousand
     * milliseconds in a second. This now gives 499 ms for a requested 500. */
    const uint32_t iterations = (hz / 8000u) * ms;

    for (volatile uint32_t i = 0; i < iterations; i++) {
        __asm volatile ("nop");
    }

    /* Unconditionally false, and NOT g_core_hz != 0u, which is what this line
     * was until Friday 2 October 2026 and which silently became true the moment
     * the clock was decoded.
     *
     * That was a real defect and the comment above it was the symptom: it argued
     * at length that this function still refuses, while the code had quietly
     * started agreeing to be trusted. The two approximations were never
     * independent of each other in the prose and were in the code.
     *
     * It was caught by the console. P01 prints a note on every twentieth cycle
     * while this returns false, the note never appeared, and the only way that
     * happens is if this returned true. A board that can talk finds things a
     * board that only blinks cannot.
     *
     * This becomes conditional again when a caller can be told the truth, which
     * means when the cycles per iteration has a counted reference rather than an
     * estimate. That is P06, which is the project that exists to supply it. */
    return false;
}
