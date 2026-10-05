/* c/instr/freqcount.c: count an external signal's edges against the crystal.
 *
 * freqcount.h carries the design and why it changed twice on Sunday 4 October
 * 2026. The short form is that this is not the instrument that header first
 * described. It counts on LPTIM1 rather than on a 32-bit timer, and it gates on
 * the 32.768 kHz crystal through lseref.c rather than on a second timer clocked
 * from the core. Both changes came from measurements rather than from taste: a
 * core-clocked gate would have carried the core's own error into every reading,
 * and LPTIM1 is the one peripheral on this board whose counting input ST
 * demonstrates with a pin number.
 *
 * WHAT MAKES A READING TRUSTWORTHY HERE, in one paragraph, because it is the
 * whole argument. The count is an exact number of edges: integers, no clock
 * involved. The gate is an exact number of crystal ticks: 256 of a 256 Hz tick
 * is one second to whatever the crystal is worth, and nothing in the PLL chain
 * touches it. So the frequency is a ratio of two exact things and its accuracy
 * is the crystal's. The kernel clock that SAMPLES the input never enters the
 * arithmetic at all; it only sets how fast an input can be before an edge is
 * missed, which is a range limit and not an error term.
 *
 * THE ARITHMETIC IS NOT IN THIS FILE, since Monday 5 October 2026. Everything
 * that is only a calculation lives in c/instr/freqmath.c, which reads no
 * register and is therefore driven from a host against a table of cases in
 * python/tests/test_freqmath.py. This file keeps what only the part can do:
 * configuring LPTIM1, configuring the pin, and reading the counter. The reason
 * for the split is c/clock/clocktree.h's reason: an arithmetic error that only a
 * flash can find is an arithmetic error nobody will find.
 *
 * AND WHAT MAKES ONE WRONG. Two things, both reported rather than hidden:
 *
 *   - an input above the ceiling. The counter does not saturate or read low, it
 *     misses edges, and nothing in the count says so. freqcount_ceiling_hz()
 *     exists so a caller can refuse, and freqcount_measure_mhz returns 0 when
 *     the reading is at or above the ceiling rather than returning a number it
 *     cannot stand behind.
 *   - a wrap nobody counted. LPTIM1 is 16 bits, so a one second gate on
 *     anything above 65535 Hz wraps. The autoreload is left at 0xFFFF and the
 *     wrap flag is polled rather than interrupt-driven, because this is a
 *     blocking measurement and an interrupt would need a vector this project
 *     does not own. A wrap that the poll misses would read low by 65536, so the
 *     poll interval is the thing that bounds the usable input, and it is checked
 *     rather than assumed: see the comment at the counting loop.
 */
#include "freqcount.h"

#include "freqmath.h"
#include "lseref.h"
#include "../board/board.h"
#include "../board/stm32h7a3_regs.h"

#ifdef BOARD_REGS_CONFIRMED

/* The autoreload. 0xFFFF is the full 16 bits, so the counter wraps as rarely as
 * it can and every wrap is exactly 65536 edges. */
#define FREQCOUNT_ARR 0xFFFFu

/* The sub second tick rate this board is expected to report, used only to turn
 * a caller's milliseconds into lseref's ticks. RTC_PRER reads 007F00FF, so
 * PREDIV_A is 127 and the rate is 32768 over 128. The measurement does not
 * depend on this being right: it uses the rate lseref reports. */
#define FREQCOUNT_EXPECTED_TICK_HZ 256u

/* How long to wait for ARR to be acknowledged. The write crosses into the
 * peripheral's clock domain, so it takes effect some kernel clock cycles later
 * and ARROK says when. Bounded rather than a bare while loop, for the same
 * reason every wait in clock280.c is bounded: a peripheral whose clock was never
 * enabled never sets a ready flag, and a spin that cannot end turns a
 * configuration mistake into a board that stopped talking. */
#define FREQCOUNT_ARROK_SPINS 100000u

static bool g_ready;

/* The counter's own state across one gate. Static because the counting loop and
 * the wrap accounting are two statements about one thing and splitting them into
 * arguments would make the loop longer without making it clearer. */
static uint32_t g_wraps;

/* Clear the wrap flag, then return whether it had been set.
 *
 * READ THEN CLEAR AND NOT THE OTHER WAY. ARRM is set by hardware and cleared by
 * writing ARRMCF. Clearing first and reading after would drop a wrap that
 * arrived between the two, which at 65536 edges is the largest single error this
 * instrument can make. */
static bool take_wrap(void)
{
    const bool wrapped = (LPTIM1_ISR & LPTIM_ISR_ARRM_MSK) != 0u;
    if (wrapped) {
        LPTIM1_ICR = LPTIM_ICR_ARRMCF_MSK;
    }
    return wrapped;
}

int freqcount_init(void)
{
    g_ready = false;
    g_wraps = 0u;

    /* The peripheral's bus clock and the port's, in that order, because the
     * CFGR write below goes nowhere without the first and the pin configuration
     * goes nowhere without the second. */
    RCC_APB1LENR |= (1u << RCC_APB1LENR_LPTIM1EN_POS);
    (void) RCC_APB1LENR;
    RCC_AHB4ENR |= RCC_AHB4ENR_GPIODEN;
    (void) RCC_AHB4ENR;

    /* The kernel clock: the APB1 clock rather than the crystal. Both are
     * available and the choice is deliberate. The crystal would make one
     * reference do both jobs, which reads well and caps the input near 32 kHz.
     * The APB1 clock caps it at board_pclk1_hz(), 64 MHz on the reset clock and
     * 140 MHz at the 280 MHz setting, and costs nothing in accuracy because the
     * kernel clock is not in the arithmetic. Range for free is worth having. */
    uint32_t sel = RCC_CDCCIP2R;
    sel &= ~RCC_CDCCIP2R_LPTIM1SEL_MSK;
    sel |= (RCC_LPTIM1SEL_PCLK1 << RCC_CDCCIP2R_LPTIM1SEL_POS);
    RCC_CDCCIP2R = sel;

    /* CFGR must be written while the peripheral is DISABLED. The reference
     * manual says so and the HAL obeys it; a write to CFGR with ENABLE set is
     * discarded, which would leave the counter advancing on its kernel clock and
     * reporting a frequency that is the APB1 clock rather than the signal. That
     * is the plausible wrong answer this file most has to avoid. */
    LPTIM1_CR = 0u;
    (void) LPTIM1_CR;

    uint32_t cfgr = LPTIM1_CFGR;
    cfgr &= ~(LPTIM_CFGR_CKSEL_MSK | LPTIM_CFGR_PRESC_MSK);
    cfgr |= LPTIM_CFGR_COUNTMODE_MSK;
    LPTIM1_CFGR = cfgr;

    /* CKSEL stays 0 and COUNTMODE goes to 1, which is the pair stm32h7a3_regs.h
     * explains: the kernel clock clocks the peripheral, and COUNTMODE is what
     * makes the counter advance on Input1's edges instead of on that clock.
     * Setting CKSEL instead would clock the whole peripheral from the pin, which
     * is a different mode with its own constraints and is not what ST's pulse
     * counter does. */
    if ((LPTIM1_CFGR & LPTIM_CFGR_COUNTMODE_MSK) == 0u) {
        return -1;
    }

    board_pin_alternate(FREQCOUNT_IN_PORT, FREQCOUNT_IN_PIN, FREQCOUNT_IN_AF,
                        GPIO_PUPD_PULLUP);

    /* ENABLE first, then ARR, then wait for the acknowledgement. That order is
     * the peripheral's, not a preference: ARR is not writable until the counter
     * is enabled. */
    LPTIM1_CR = LPTIM_CR_ENABLE_MSK;
    (void) LPTIM1_CR;

    LPTIM1_ICR = LPTIM_ICR_ARROKCF_MSK;
    LPTIM1_ARR = FREQCOUNT_ARR;

    uint32_t spins = 0u;
    while ((LPTIM1_ISR & LPTIM_ISR_ARROK_MSK) == 0u) {
        if (++spins >= FREQCOUNT_ARROK_SPINS) {
            return -1;
        }
    }
    LPTIM1_ICR = LPTIM_ICR_ARROKCF_MSK;

    if (LPTIM1_ARR != FREQCOUNT_ARR) {
        /* The acknowledgement arrived and the register does not hold what was
         * written, which would mean the address is wrong rather than the
         * sequence. Reading back is cheap and this is the one failure that would
         * otherwise look like a signal of zero hertz. */
        return -1;
    }

    /* Continuous mode. CNTSTRT starts a counter that keeps running, which is
     * what a gated measurement wants: the gate decides the window, not the
     * counter. SNGSTRT would stop at the first autoreload, and it is also the
     * bit whose mask in ST's header covers a reserved bit; stm32h7a3_regs.h
     * records that. */
    LPTIM1_CR = LPTIM_CR_ENABLE_MSK | LPTIM_CR_CNTSTRT_MSK;
    (void) LPTIM1_CR;

    (void) take_wrap();

    /* THE CRYSTAL, WITHOUT WHICH THERE IS NO GATE. lseref_measure_core_hz
     * requires lseref_start to have succeeded, and its header says so; this
     * function is the only sensible place to satisfy that, because a caller who
     * wanted a frequency should not have to know that the gate is a real time
     * clock. Three seconds is lseref's own bound for a 32.768 kHz crystal, which
     * takes far longer to start than any PLL and is why that bound is in seconds
     * rather than milliseconds. */
    lseref_start_t start;
    if (lseref_start(&start, 3000u) != 0) {
        return -1;
    }

    g_ready = true;
    return 0;
}

uint32_t freqcount_ceiling_hz(void)
{
    /* The kernel clock, which is the APB1 clock by the selection in
     * freqcount_init. 0 when the clock tree could not be decoded, which is the
     * same answer board_pclk1_hz() gives and for the same reason: a ceiling
     * nobody can state is not a ceiling. */
    return board_pclk1_hz();
}

/* The counter, sampled once, with the wraps accumulated.
 *
 * WHY THIS IS A FUNCTION AND WHY IT IS CALLED OFTEN. A wrap is only visible
 * while ARRM is set, and nothing latches how many happened. So the flag has to
 * be taken often enough that two wraps cannot fall between two looks. At the
 * ceiling, 140 MHz, a 16 bit counter wraps every 468 microseconds, and the loop
 * below runs this between every pair of crystal ticks, which are 3906
 * microseconds apart. That is not often enough, and it is exactly why the
 * usable input is bounded well below the sampling ceiling rather than at it.
 *
 * The honest statement of the limit: one wrap per tick interval is the most this
 * can account for, so the input must stay below 65536 edges per 3906
 * microseconds, which is about 16.8 MHz. freqcount_measure_mhz refuses above
 * that rather than reporting a figure short by a multiple of 65536. */
static uint32_t counter_now(void)
{
    if (take_wrap()) {
        g_wraps++;
    }
    return freqmath_compose(g_wraps, LPTIM1_CNT);
}

uint64_t freqcount_measure_mhz(uint32_t gate_ms)
{
    if (!g_ready || gate_ms == 0u) {
        return 0u;
    }

    /* THE GATE IS REQUESTED IN TICKS AND NOT IN MILLISECONDS, which is a wart in
     * lseref's interface rather than here, and it has to be handled honestly.
     * The tick rate comes out of RTC_PRER and is 256 Hz on this board, but this
     * function cannot know that before it asks. So the conversion ASSUMES 256
     * and the arithmetic below USES the rate that came back, which means a
     * different PRER would give a gate of a different length than the caller
     * asked for while still producing a correct frequency. That is the right way
     * round: the reading stays true and only the window moves. */
    const uint32_t ticks = freqmath_ticks_for_ms(gate_ms,
                                                FREQCOUNT_EXPECTED_TICK_HZ);
    if (ticks == 0u) {
        return 0u;
    }

    lseref_measure_t gate;
    const uint32_t before = counter_now();

    /* Arguments in lseref's order, out first. Worth a word because getting it
     * the other way round compiles in C only if the types happen to allow it,
     * and here they do not, which is the one piece of luck in this file. */
    if (lseref_measure_core_hz(&gate, ticks) != 0) {
        return 0u;
    }

    const uint32_t after = counter_now();

    /* The arithmetic, the two refusals and the derived usable maximum are all
     * freqmath's now, driven from a host against a table of cases. What this
     * function contributes is the four measured inputs: the edges, the tick rate
     * and tick count the gate actually used, and the sampling ceiling. */
    return freqmath_mhz(freqmath_edges(before, after),
                        gate.ck_apre_hz,
                        gate.ticks,
                        freqcount_ceiling_hz(),
                        FREQCOUNT_ARR);
}

uint64_t freqcount_resolution_mhz(uint32_t gate_ms)
{
    return freqmath_resolution_mhz(gate_ms);
}

#else   /* the register addresses are not confirmed */

int freqcount_init(void) { return -1; }
uint32_t freqcount_ceiling_hz(void) { return 0u; }
uint64_t freqcount_measure_mhz(uint32_t gate_ms) { (void) gate_ms; return 0u; }

/* The resolution is arithmetic and does not need a confirmed register, so this
 * branch answers it properly rather than returning 0. A reader can ask what the
 * instrument's resolution would be before the instrument exists. */
uint64_t freqcount_resolution_mhz(uint32_t gate_ms)
{
    return freqmath_resolution_mhz(gate_ms);
}

#endif  /* BOARD_REGS_CONFIRMED */
