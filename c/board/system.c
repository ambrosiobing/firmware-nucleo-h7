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

/* THE loop, and the reason it is a function.
 *
 * The calibration below and board_delay_ms() both call this and nothing else, so
 *
 * MEASURED AT 9.01 CYCLES on Friday 2 October 2026, not the 8.00 a host PC's clock
 * gave earlier the same day, and the difference is the point rather than a
 * problem. That 8.00 was for an INLINE loop inside board_delay_ms. Moving the loop
 * into this function so both paths would share it changed the generated code,
 * because a volatile counter inside a called function is addressed differently
 * from one in the enclosing function, and the cost went from eight cycles to nine.
 *
 * Had the measured constant 8000 iterations per millisecond survived that
 * refactor, every interval would have stretched by 12.5 per cent, 562 ms for a
 * requested 500, with nothing in the build to say so. The calibration absorbed it
 * at startup without being told. The warning in the next paragraph was
 * demonstrated on the very commit that introduced it.
 * by construction they cost the same per iteration. Calibrating one loop and then
 * timing a different one is the classic way to get this wrong: the compiler is
 * free to unroll, reorder or register-allocate two textually identical loops
 * differently depending on what surrounds them, and the error is silent.
 *
 * The counter is volatile, which forces a load and a store to memory on every
 * iteration rather than keeping it in a register. That is what makes the loop
 * cost eight cycles rather than four, and it is deliberate: a loop the compiler
 * can see through can be deleted entirely. */
static void spin(uint32_t iterations)
{
    for (volatile uint32_t i = 0u; i < iterations; i++) {
        __asm volatile ("nop");
    }
}

/* Measured iterations per millisecond. 0 means calibration did not happen, which
 * is the only thing that now makes board_delay_ms() refuse. */
static uint32_t g_iters_per_ms;

void board_delay_calibrate(void)
{
    g_iters_per_ms = 0u;

    /* Both are required and neither can be assumed. Without the counter there is
     * nothing to measure against; without an established frequency a cycle count
     * cannot become a time. */
    if (!board_cycles_available() || g_core_hz == 0u) {
        return;
    }

    /* TWO measurements, n and 2n, and the difference is what is used. The first
     * version of this timed one probe and divided, and the comment here claimed
     * that the two counter reads and the function call were a rounding error.
     * They were not. They are 163 cycles, measured on Friday 2 October 2026, and
     * that is the whole of a 0.18 per cent bias:
     *
     *   calibration saw     9.0166 cycles per iteration
     *   the delay actually  9.0003 cycles per iteration
     *   163 / 10000 iterations = 0.0163, which is the difference exactly
     *
     * The overhead is real and unavoidable: board_cycles_now() and spin() are in
     * different translation units and nothing inlines at -Os, so each call costs
     * a branch, a prologue and an epilogue, and the load from the private
     * peripheral bus at 0xE0001004 is itself several cycles. All of it falls
     * inside the measured window.
     *
     * Timing n and then 2n removes it without having to know what it is. Both
     * windows contain exactly the same fixed cost, so subtracting one from the
     * other leaves precisely n iterations. This is the standard cure and it is
     * worth stating that it needs no estimate of the overhead at all: an
     * unmeasured constant cancels against itself.
     *
     * TWO THOUSAND, reduced from ten thousand on Saturday 3 October 2026, and the
     * reason is a measurement rather than a preference. P01 reports the time from
     * the first instruction of C to its first printed character, and that read
     * 11.989 ms of which about 11.9 ms was this calibration: four spins of n, n
     * and 2n, which is 40000 iterations, at the 19 cycles the loop happened to
     * cost in that build. Ninety-nine per cent of startup spent measuring the
     * delay is a poor trade.
     *
     * Precision does not suffer. At 2000 iterations the difference window is tens
     * of thousands of cycles, so a one cycle error in either reading is a few parts
     * in a hundred thousand, far below the oscillator's own tolerance. The figure
     * it produced at 10000 and at 2000 is the same.
     *
     * Still nowhere near the counter's 67 second wrap at 64 MHz. */
    const uint32_t n = 2000u;

    /* A WARM-UP, untimed, and the reason it is needed is that two-point
     * subtraction cancels a constant and a warm-up is not one.
     *
     * Measured on Friday 2 October 2026: the two-point version without this read
     * 8.99 cycles per iteration where the loop actually costs 9.0003, understating
     * it by about 53 cycles over n iterations and making every delay 0.058 per
     * cent LONG. The sign is the clue. The short window ran the loop for the first
     * time, with flash wait states, a cold branch predictor and a cold prefetch
     * buffer; the long window ran immediately afterwards with the code warm. So
     *
     *   short = n*9 + overhead + warmup
     *   long  = 2n*9 + overhead
     *   long - short = n*9 - warmup
     *
     * and the warm-up subtracts rather than cancels. Paying it once before either
     * measurement puts both windows in the same state, which is the only thing the
     * subtraction actually requires of them.
     *
     * This is the second refinement of this measurement and both were found the
     * same way: a residual with a consistent sign, small enough to be dismissed
     * and reproducible enough not to be. */
    spin(n);

    const uint32_t a0 = board_cycles_now();
    spin(n);
    const uint32_t a1 = board_cycles_now();

    const uint32_t b0 = board_cycles_now();
    spin(2u * n);
    const uint32_t b1 = board_cycles_now();

    /* Unsigned subtraction, correct across one wrap of the 32 bit counter. */
    const uint32_t short_window = a1 - a0;   /* n iterations  + overhead */
    const uint32_t long_window  = b1 - b0;   /* 2n iterations + the same overhead */

    /* The long window must exceed the short one by roughly the short one. If it
     * does not, something is wrong enough that no figure should be published:
     * the counter stopped, the loop was optimised away, or a wrap landed between
     * the reads. Refusing leaves board_delay_ms returning false, which is the
     * behaviour this whole mechanism replaced and is still the safe answer. */
    if (long_window <= short_window) {
        return;
    }
    const uint32_t cycles_n = long_window - short_window;   /* exactly n */
    if (cycles_n == 0u) {
        return;
    }

    /* iterations per ms = n * (cycles in one ms) / cycles for n iterations.
     *
     * The multiplication is checked rather than hoped for: n is 10000 and
     * g_core_hz/1000 is at most 280000 at this part's top speed, so the product
     * is at most 2.8e9, inside a uint32. At 64 MHz it is 6.4e8. */
    g_iters_per_ms = (n * (g_core_hz / 1000u)) / cycles_n;
}

uint32_t board_delay_iters_per_ms(void)
{
    return g_iters_per_ms;
}

bool board_delay_ms(uint32_t ms)
{
    /* CALIBRATED, and this is the function finally able to return true.
     *
     * It refused all day on Friday 2 October 2026 and the refusal was right. Two
     * approximations stood in it: the core frequency, settled that morning by
     * decoding RCC, and the cycles per iteration, which was a guess of four,
     * measured from a host PC's clock as eight, and corrected to a constant. A
     * measured constant was still the wrong answer, because eight cycles holds
     * for one compiler at one optimisation level with this code in flash at this
     * clock, and nothing in the build checks that it still does.
     *
     * So it is measured at startup instead, on the actual build, against the
     * core's own counter, using the same spin() the delay itself calls. Change the
     * optimisation level and the figure changes with it. The remaining uncertainty
     * is the oscillator's absolute accuracy, which is a stated tolerance rather
     * than an unknown factor: nominal 64 MHz, measured 64.17 MHz on this board.
     * A caller wanting to know which clock asks board_clock_status().
     *
     * That is the difference between a figure that is correct and a figure that is
     * known to be correct, and it is the whole reason this can now return true.
     *
     * One limit worth naming: at the measured 8000 iterations per millisecond the
     * product below overflows a uint32 above about 536,000 ms, which is nine
     * minutes. Nothing busy-waits for nine minutes, and if it did, the counter
     * would be the wrong instrument anyway. */
    if (g_iters_per_ms != 0u) {
        spin(g_iters_per_ms * ms);
        return true;
    }

    /* Uncalibrated. Still delays, because a blinking LED is more useful than a
     * refusal during bring-up, and still returns false so that nothing times
     * anything with it. The divisor is the figure measured on
     * Friday 2 October 2026, kept only as a fallback. */
    const uint32_t hz = g_core_hz ? g_core_hz : HSI_HZ_NOMINAL;
    spin((hz / 8000u) * ms);
    return false;
}
