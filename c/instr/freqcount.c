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
 *     anything above 65535 Hz wraps, and ARRM is a FLAG rather than a count: it
 *     says one or more matches happened since it was last cleared, and nothing
 *     latches how many. Until Tuesday 6 October 2026 this file read that flag
 *     twice per measurement, once at each end of the gate, so a gate spanning
 *     fifteen matches contributed one and any one second reading was capped at
 *     131071 Hz. The board showed it: 87903 millihertz against a megahertz.
 *     The flag is now read inside the gate loop, thousands of times per tick,
 *     and cleared on every match so that the next one is visible. See the loop.
 *
 * THAT FIX ARRIVED SECOND, AFTER A TEST THAT COULD HAVE REFUTED IT. A gate of
 * twelve ticks is too short for a megahertz to wrap twice, so the two-sample
 * scheme was exact over it; if the short gate had also read 65536 plus a
 * residue, the model would have been wrong and this edit would have been the
 * wrong edit. It read 1041579, 1041877 and 1032768 Hz where the refuting band
 * was 65000 to 131000. projects/P01-toolchain-first-light/README.md carries the
 * numbers.
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

/* How many polls to allow while waiting for one crystal tick.
 *
 * Bounded in polls rather than in time, for lseref's reason: time is what is
 * being measured. One iteration of the waiting loop below reads LPTIM1_ISR and
 * RTC_SSR, two accesses across a peripheral bus, so call it fifty core cycles.
 * At the reset clock a 256 Hz tick is 250000 cycles, which is about five
 * thousand iterations, and at 280 MHz it is 1093750, which is about twenty two
 * thousand. So ten million is a margin of two thousand at the reset clock and
 * about four hundred and fifty at 280 MHz, and the TIGHTER margin is at the
 * faster clock because more iterations fit inside one tick there. Running out is
 * reported rather than hung: a real-time clock whose tick never arrives would
 * otherwise spin here for as long as the board is powered.
 *
 * The same figure read the other way is the bound on this instrument's input.
 * Fifty cycles at 64 MHz is 780 nanoseconds, and a 16-bit counter takes 3.9
 * milliseconds to wrap at the stated usable maximum of 16777216 Hz, so the poll
 * is five thousand times faster than it needs to be. That margin is the point
 * of polling here instead of once per tick, where the two figures were 3.906 ms
 * against 3.9 ms and a second wrap could hide inside the first. */
#define FREQCOUNT_TICK_POLLS_MAX  10000000u

/* How many times to re-take a boundary sample whose wrap flag was ambiguous.
 *
 * A sample is ambiguous when the flag becomes set between the counter read and
 * the check just after it, because then which side of the read the match fell
 * on is not knowable. The window is a few core cycles wide, so one retry is
 * already more than the hardware will ask for; eight consecutive ambiguous
 * samples is not reachable below the usable maximum and is reported as a
 * refusal rather than resolved by a guess. */
#define FREQCOUNT_SAMPLE_ATTEMPTS  8u

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

/* How many passes the waiting loop took per tick, over the last gate. Static
 * for the same reason and read back by freqcount_last_poll, whose declaration
 * carries the argument for measuring this at all. */
static freqcount_poll_t g_poll;

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

    /* THE CRYSTAL, WITHOUT WHICH THERE IS NO GATE. lseref_tick_open requires
     * lseref_start to have succeeded, and its header says so; this function is
     * the only sensible place to satisfy that, because a caller who wanted a
     * frequency should not have to know that the gate is a real time clock.
     * Three seconds is lseref's own bound for a 32.768 kHz crystal, which
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

/* Wait for the next crystal tick, counting every wrap that happens while
 * waiting. Returns false if the tick never arrived.
 *
 * THIS IS THE WHOLE FIX. The wrap flag is read on every pass of this loop, not
 * once per gate and not once per tick, and every match found is cleared so that
 * the next one is visible. The loop is already waiting; reading a flag while it
 * waits costs nothing and is the only thing that makes `wraps` a count rather
 * than a boolean.
 *
 * THE ORDER INSIDE THE LOOP IS NOT FREE. The flag is taken BEFORE the tick mark
 * is read, so that the pass which notices the tick has already accounted for any
 * wrap that preceded it. Reading the mark first and returning would leave a
 * match sitting in the flag for the boundary sample to deal with, which is where
 * the next function comes in, and that function is written for it.
 *
 * AND NOT AN INTERRUPT. An ARRM interrupt would be the same defect in a
 * different place: the flag still says one or more rather than how many, so a
 * handler that ran late by one autoreload would lose a match exactly as two
 * samples did. At the usable maximum a wrap takes 3.9 ms against a 3.906 ms
 * tick, which is precisely where a handler driven at tick rate could be late by
 * one. A poll inside the loop that is already running has no such latency, and
 * it adds no vector, no priority decision and no NVIC entry to a project that
 * has not yet earned any of the three. */
static bool wait_tick(uint32_t *mark, uint32_t *passes)
{
    const uint32_t was = *mark;
    uint32_t polls = 0u;

    while (polls < FREQCOUNT_TICK_POLLS_MAX) {
        uint32_t now;

        if (take_wrap()) {
            g_wraps++;
        }
        now = lseref_tick_mark();
        if (now != was) {
            *mark = now;
            /* polls counted the passes that did NOT see the tick, so the pass
             * that did is one more. Reporting polls alone would be short by one
             * per tick, which at twenty thousand passes is five parts in a
             * hundred thousand of the figure and is free to get right. */
            *passes = polls + 1u;
            return true;
        }
        polls++;
    }
    return false;
}

/* Sample the counter at a gate boundary, with the wrap flag resolved rather
 * than guessed. Returns false if it could not be resolved.
 *
 * WHAT IS BEING RESOLVED. Two reads are involved, the flag and the counter, and
 * the counter can wrap between them. Three outcomes, and each has to be handled
 * differently or 65536 edges go missing or arrive twice:
 *
 *   - the flag is already set on entry. That match happened before this sample,
 *     so it belongs to the interval that is closing. It is counted and cleared,
 *     and the sample is taken again now that the flag is clean.
 *   - the flag is clear, the counter is read, and the flag is still clear. The
 *     reading is unambiguous and is returned.
 *   - the flag is clear, the counter is read, and the flag is set afterwards.
 *     Whether the match fell before or after the read is not knowable, so the
 *     reading is discarded and the next pass counts the match and reads again.
 *     The retry costs a few cycles, which is the one place where this sample
 *     moves away from the tick boundary. Twenty cycles at 280 MHz is 71
 *     nanoseconds, which against the shortest gate this volume runs, 46.875 ms,
 *     is 1.5 parts per million, and against a one second gate is 0.07. It is
 *     also not paid on every sample but only on the rare pass where the flag
 *     was ambiguous.
 *
 * The caller decides what the counted wraps mean. At the gate's opening
 * boundary they belong to whatever came before and are discarded by zeroing the
 * count after this returns; at the closing boundary they are inside the gate and
 * are kept. That decision is at the call site because it is the only place the
 * two boundaries look different. */
static bool sample_counter(uint32_t *out)
{
    uint32_t attempt;

    for (attempt = 0u; attempt < FREQCOUNT_SAMPLE_ATTEMPTS; attempt++) {
        uint32_t cnt;

        if (take_wrap()) {
            g_wraps++;
            continue;
        }

        /* READ ONCE, AND ONE QUESTION LEFT OPEN. RM0455 carries a rule that an
         * LPTIM counter clocked asynchronously must be read twice and the two
         * values compared, and whether it applies in COUNTMODE with the kernel
         * clock taken from PCLK1, which is this configuration, is not sourced
         * here. A blind double read is not the safe choice it looks like: at the
         * usable maximum the counter changes every 60 nanoseconds, so two reads
         * legitimately disagree, and a loop demanding agreement would refuse
         * fast inputs for a reason that has nothing to do with the hardware.
         * Reading RM0455's LPTIM chapter is what settles this, and it is not
         * read yet, so the single read stands and the question is written down
         * rather than answered by preference. */
        cnt = LPTIM1_CNT;

        if ((LPTIM1_ISR & LPTIM_ISR_ARRM_MSK) == 0u) {
            *out = cnt;
            return true;
        }
    }
    return false;
}

/* THIS FUNCTION OWNS THE GATE, since Tuesday 6 October 2026, and that is the
 * change rather than an implementation detail of it.
 *
 * WHAT IT USED TO DO. It sampled the counter, called lseref_measure_core_hz to
 * pass the time, and sampled again. Two defects came out of those three lines
 * and both were visible on the board the first day the counter counted:
 *
 *   - the wrap count could only reach one, because the flag was read twice,
 *     which capped a one second reading at 131071 Hz
 *   - the edge window was WIDER THAN THE GATE IT WAS DIVIDED BY, because
 *     lseref_measure_core_hz waits for its own tick boundary before it starts
 *     counting and that wait sat inside the counter's window while the divisor
 *     was only `ticks`. A twelve tick gate measured 12.5 ticks long, which is an
 *     extra half tick on average and is exactly what a uniform wait of nought to
 *     one tick looks like
 *
 * The second was the better find and it was free: the short gate's parts per
 * million should have equalled the clock offset printed beside it, because the
 * source follows this part's clock while the gate is the crystal, and it came
 * back 32768 to 41877 ppm out instead. Nothing was designed to catch that. Two
 * instruments were pointed at one quantity and they disagreed.
 *
 * WHAT IT DOES NOW. It opens the tick source, waits for a boundary so that the
 * first sample sits on one, samples, waits exactly `ticks` boundaries while
 * polling the wrap flag between them, and samples again. The gate is the
 * interval between those two samples and the divisor is the number of ticks that
 * interval contains, with nothing else in between. The core is not measured here
 * at all any more; a caller who wants the core frequency asks lseref for it,
 * which is the right place to ask.
 *
 * THE PASS CONDITION, written here because a fix without one is a hope. The
 * short gate's parts per million must come out equal to the clock offset, same
 * sign and same size, within the 21 Hz that a twelve tick gate resolves. Not a
 * return to 1000000000 millihertz: the source is not exactly a megahertz, it is
 * APB2 over a whole number and so it carries the clock's own error, and a fix
 * judged against a round number would be judged against the wrong number. */
uint64_t freqcount_measure_mhz(uint32_t gate_ms)
{
    lseref_tick_t tick;
    uint32_t mark;
    uint32_t before;
    uint32_t after;
    uint32_t ticks;
    uint32_t n;
    uint32_t align_passes = 0u;   /* the alignment wait's count, deliberately
                                   * discarded: it starts mid tick */

    if (!g_ready || gate_ms == 0u) {
        return 0u;
    }

    /* The tick source, opened on every measurement rather than once at init, so
     * that the rate going into the arithmetic below comes from the same register
     * read as the gate it describes. It also removes a wart this function used
     * to carry: the millisecond-to-tick conversion had to ASSUME 256 Hz because
     * the rate was not known until lseref had already been called. It is known
     * before the conversion now. */
    if (lseref_tick_open(&tick) != 0) {
        return 0u;
    }

    ticks = freqmath_ticks_for_ms(gate_ms, tick.ck_apre_hz);
    if (ticks == 0u) {
        return 0u;
    }

    /* ALIGN FIRST. This wait is the one that used to be inside the measurement
     * and outside the divisor. It is still here, because a first sample taken
     * wherever the call happened to land would make the gate a fraction of a
     * tick longer than `ticks` says, but it is now OUTSIDE the counter's window:
     * nothing is sampled until it has finished. Wraps found while waiting are
     * counted by wait_tick and then thrown away with the zeroing below, because
     * they happened before the gate opened. */
    g_poll.ok          = false;
    g_poll.ticks       = 0u;
    g_poll.tick_hz     = 0u;
    g_poll.polls_min   = 0xFFFFFFFFu;
    g_poll.polls_max   = 0u;
    g_poll.polls_total = 0u;

    mark = lseref_tick_mark();
    if (!wait_tick(&mark, &align_passes)) {
        return 0u;
    }

    (void) align_passes;

    /* The gate opens on this sample. The zeroing comes AFTER it, not before,
     * because sample_counter counts any match it has to clear on its way to an
     * unambiguous reading, and at this boundary those matches belong to the time
     * before the gate. */
    if (!sample_counter(&before)) {
        return 0u;
    }
    g_wraps = 0u;

    for (n = 0u; n < ticks; n++) {
        uint32_t passes = 0u;

        if (!wait_tick(&mark, &passes)) {
            return 0u;
        }
        if (passes < g_poll.polls_min) {
            g_poll.polls_min = passes;
        }
        if (passes > g_poll.polls_max) {
            g_poll.polls_max = passes;
        }
        g_poll.polls_total += passes;
    }
    g_poll.ticks   = ticks;
    g_poll.tick_hz = tick.ck_apre_hz;
    g_poll.ok      = true;

    /* And closes on this one, on the tick the loop just saw. Here the matches
     * sample_counter clears ARE inside the gate and are kept. */
    if (!sample_counter(&after)) {
        return 0u;
    }

    /* The arithmetic, the two refusals and the derived usable maximum are all
     * freqmath's, driven from a host against a table of cases. What this
     * function contributes is the four measured inputs: the edges, the tick rate
     * and the tick count of the gate it just ran, and the sampling ceiling.
     *
     * THE EDGES ARE wraps TIMES 65536 PLUS after MINUS before, AND THE BORROW IS
     * THE MODULAR SUBTRACTION'S. Composing `before` with a wrap count of zero is
     * what makes that true: the wraps are counted from the opening sample, so
     * they sit above the opening residue, and when the closing residue is the
     * smaller of the two the subtraction borrows one 65536 out of the wrap field
     * on its own. python/tests/test_freqmath.py drives this composition in the
     * shape this call site uses it, borrow included. */
    return freqmath_mhz(freqmath_edges(freqmath_compose(0u, before),
                                       freqmath_compose(g_wraps, after)),
                        tick.ck_apre_hz,
                        ticks,
                        freqcount_ceiling_hz(),
                        FREQCOUNT_ARR);
}

uint64_t freqcount_resolution_mhz(uint32_t gate_ms)
{
    return freqmath_resolution_mhz(gate_ms);
}

freqcount_poll_t freqcount_last_poll(void)
{
    return g_poll;
}

#else   /* the register addresses are not confirmed */

int freqcount_init(void) { return -1; }
uint32_t freqcount_ceiling_hz(void) { return 0u; }
uint64_t freqcount_measure_mhz(uint32_t gate_ms) { (void) gate_ms; return 0u; }

freqcount_poll_t freqcount_last_poll(void)
{
    const freqcount_poll_t none = { false, 0u, 0u, 0u, 0u, 0u };
    return none;
}

/* The resolution is arithmetic and does not need a confirmed register, so this
 * branch answers it properly rather than returning 0. A reader can ask what the
 * instrument's resolution would be before the instrument exists. */
uint64_t freqcount_resolution_mhz(uint32_t gate_ms)
{
    return freqmath_resolution_mhz(gate_ms);
}

#endif  /* BOARD_REGS_CONFIRMED */
