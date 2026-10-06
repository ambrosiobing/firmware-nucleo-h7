/* c/instr/lseref.c: start the 32.768 kHz crystal, and say what happened.
 *
 * See lseref.h for why this exists. In one line: it is the only reference on
 * this board that does not come from the PLL chain, so it is the only thing that
 * can turn 280 MHz from derived into measured.
 */
#include "lseref.h"

#include <stddef.h>

#include "board.h"
#include "stm32h7a3_regs.h"

#ifdef BOARD_REGS_CONFIRMED

int lseref_start(lseref_start_t *out, uint32_t timeout_ms)
{
    uint32_t waited = 0u;

    if (out == NULL) {
        return -1;
    }

    /* As found, before anything is written. The backup domain survives a system
     * reset, so this is not necessarily a reset value and the record has to
     * carry it: without it, a zero wait is ambiguous between "it started
     * instantly", which no 32.768 kHz crystal does, and "it was already on",
     * which is the usual case after a reset button press. */
    out->bdcr_before     = RCC_BDCR;
    out->bdcr_after      = out->bdcr_before;
    out->already_running = ((out->bdcr_before
                             & (RCC_BDCR_LSEON_MSK | RCC_BDCR_LSERDY_MSK))
                            == (RCC_BDCR_LSEON_MSK | RCC_BDCR_LSERDY_MSK));
    out->started         = ((out->bdcr_before & RCC_BDCR_LSERDY_MSK) != 0u);
    out->waited_ms       = 0u;

    /* The write protection first. RCC_BDCR IGNORES writes while this is clear,
     * and an ignored write is not an error: the enable would simply not happen
     * and the crystal would look absent. Read back rather than assumed, for the
     * same reason every other step in this repository reads back. */
    PWR_CR1 |= PWR_CR1_DBP_MSK;
    (void) PWR_CR1;
    out->backup_unlocked = ((PWR_CR1 & PWR_CR1_DBP_MSK) != 0u);
    if (!out->backup_unlocked) {
        return -1;
    }

    if (out->already_running) {
        out->bdcr_after = RCC_BDCR;
        return 0;
    }

    /* Enable, and touch nothing else. LSEBYP stays clear because this board has
     * a crystal and not an external clock, LSEDRV stays at its reset value
     * because a drive level this repository has not sourced is a guess, and
     * RTCSEL and RTCEN stay alone because selecting a clock for the real-time
     * clock is a different decision with its own consequences. */
    RCC_BDCR |= RCC_BDCR_LSEON_MSK;
    (void) RCC_BDCR;

    /* Milliseconds, not spin counts. A 32.768 kHz crystal starts in hundreds of
     * milliseconds and the datasheets allow a second or two, so the spin bound
     * used for a PLL ready flag, which is tens of milliseconds, would conclude
     * this crystal is broken. The granularity is board_delay_ms, which is
     * calibrated against the core clock, so this bound is only as accurate as
     * the clock under test: acceptable for a timeout and the reason the field is
     * called `waited_ms` rather than anything suggesting a measurement. */
    while (waited < timeout_ms) {
        if ((RCC_BDCR & RCC_BDCR_LSERDY_MSK) != 0u) {
            break;
        }
        (void) board_delay_ms(1u);
        waited++;
    }

    out->bdcr_after = RCC_BDCR;
    out->waited_ms  = waited;
    out->started    = ((out->bdcr_after & RCC_BDCR_LSERDY_MSK) != 0u);
    return out->started ? 0 : -1;
}

/* Bounded, and the bound is in polls rather than in time because time is what is
 * being measured. One sub second tick is about 3.9 ms at the default prescaler,
 * which at 280 MHz is a bit over a million cycles, so a few hundred thousand
 * polls. Ten million is a margin of about seventy, and running out is reported
 * rather than hung. */
#define TICK_POLLS_MAX  10000000u

/* Wait for the sub second register to change, bounded. Returns false on timeout.
 *
 * Polling a shadow register rather than an edge, which is fine and is why: the
 * shadow is refreshed once per sub second tick, so a change IS a tick, and the
 * fixed offset between the tick and this code noticing it cancels between the
 * start and the end of the gate. What does not cancel is the polling jitter,
 * which is a handful of cycles against the two hundred and eighty million in a
 * one second gate. */
static bool wait_ssr_change(uint32_t *ssr)
{
    const uint32_t was = *ssr;
    uint32_t polls = 0u;

    while (polls < TICK_POLLS_MAX) {
        const uint32_t now = RTC_SSR & RTC_SSR_SS_MSK;
        if (now != was) {
            *ssr = now;
            return true;
        }
        polls++;
    }
    return false;
}

/* Everything lseref_measure_core_hz used to do before its gate opened, lifted
 * out on Tuesday 6 October 2026 so that a second caller can have a tick source
 * without also measuring the core.
 *
 * Unchanged in substance from the version inside that function: the same four
 * steps in the same order, each refusing rather than continuing, and the tick
 * rate still computed from RTC_PRER rather than assumed. The struct is zeroed
 * first so that a refusal at step one leaves the later fields at zero, which is
 * what a reader of them expects and what the previous version did. */
static int open_tick_source(lseref_tick_t *t)
{
    uint32_t prediv_a;

    t->apb4enr        = 0u;
    t->bdcr           = 0u;
    t->prer           = 0u;
    t->icsr           = 0u;
    t->rsf_polls      = 0u;
    t->ck_apre_hz     = 0u;
    t->bus_clock_on   = false;
    t->clock_selected = false;
    t->synchronised   = false;

    /* The RTC's registers read as ZERO until this bus clock is on, which is a
     * silent failure rather than a refusal, so it is enabled and read back
     * before anything below is believed. */
    RCC_APB4ENR |= RCC_APB4ENR_RTCAPBEN_MSK;
    (void) RCC_APB4ENR;
    t->apb4enr = RCC_APB4ENR;
    t->bus_clock_on = ((t->apb4enr & RCC_APB4ENR_RTCAPBEN_MSK) != 0u);
    if (!t->bus_clock_on) {
        return -1;
    }

    /* The backup domain again, because this function does not require that
     * lseref_start() left it unlocked and a locked domain would make the clock
     * selection below a silent no-op. */
    PWR_CR1 |= PWR_CR1_DBP_MSK;
    (void) PWR_CR1;
    if ((PWR_CR1 & PWR_CR1_DBP_MSK) == 0u) {
        return -1;
    }

    /* Select the crystal, but only from nothing. RTCSEL can be written once
     * after a backup domain reset, and changing an existing selection needs a
     * backup domain reset, which would stop the crystal this measurement
     * depends on. So an existing selection that is already the crystal is
     * accepted, and any other is refused rather than forced. */
    {
        const uint32_t sel = (RCC_BDCR & RCC_BDCR_RTCSEL_MSK)
                           >> RCC_BDCR_RTCSEL_POS;
        if (sel == 0u) {
            RCC_BDCR = (RCC_BDCR & ~RCC_BDCR_RTCSEL_MSK)
                     | (RCC_RTCSEL_LSE << RCC_BDCR_RTCSEL_POS);
        } else if (sel != RCC_RTCSEL_LSE) {
            t->bdcr = RCC_BDCR;
            return -1;
        }
        RCC_BDCR |= RCC_BDCR_RTCEN_MSK;
        (void) RCC_BDCR;
        t->bdcr = RCC_BDCR;
        t->clock_selected =
            (((t->bdcr & RCC_BDCR_RTCSEL_MSK) >> RCC_BDCR_RTCSEL_POS)
             == RCC_RTCSEL_LSE)
            && ((t->bdcr & RCC_BDCR_RTCEN_MSK) != 0u);
        if (!t->clock_selected) {
            return -1;
        }
    }

    /* The shadow registers have to report themselves valid before the sub second
     * register means anything. Bounded in polls: at 256 Hz this is at most a few
     * ticks, and TICK_POLLS_MAX is a tick's worth of margin many times over. */
    {
        uint32_t polls = 0u;
        while (polls < TICK_POLLS_MAX) {
            if ((RTC_ICSR & RTC_ICSR_RSF_MSK) != 0u) {
                break;
            }
            polls++;
        }
        t->rsf_polls = polls;
        t->icsr = RTC_ICSR;
        t->synchronised = ((t->icsr & RTC_ICSR_RSF_MSK) != 0u);
        if (!t->synchronised) {
            return -1;
        }
    }

    /* The tick rate, COMPUTED FROM THE REGISTER rather than assumed. The reset
     * prescaler gives 256 Hz and nothing here changes it, but reading it is what
     * makes a non-default prescaler change the gate instead of silently scaling
     * the answer. */
    t->prer = RTC_PRER;
    prediv_a = (t->prer & RTC_PRER_PREDIV_A_MSK) >> RTC_PRER_PREDIV_A_POS;
    t->ck_apre_hz = LSE_HZ_NOMINAL / (prediv_a + 1u);
    if (t->ck_apre_hz == 0u) {
        return -1;
    }

    return 0;
}

int lseref_tick_open(lseref_tick_t *out)
{
    if (out == NULL) {
        return -1;
    }
    return open_tick_source(out);
}

uint32_t lseref_tick_mark(void)
{
    return RTC_SSR & RTC_SSR_SS_MSK;
}

int lseref_measure_core_hz(lseref_measure_t *out, uint32_t ticks)
{
    lseref_tick_t tick;
    uint32_t ssr;
    uint32_t t0;
    uint32_t t1;
    uint32_t n;
    int rc;

    if (out == NULL || ticks == 0u) {
        return -1;
    }
    out->ok                = false;
    out->core_hz_measured  = 0u;
    out->core_hz_derived   = board_core_hz();
    out->ck_apre_hz        = 0u;
    out->ticks             = ticks;
    out->cycles            = 0u;
    out->apb4enr           = 0u;
    out->bdcr              = 0u;
    out->prer              = 0u;
    out->icsr              = 0u;
    out->rsf_polls         = 0u;
    out->bus_clock_on      = false;
    out->clock_selected    = false;
    out->synchronised      = false;

    rc = open_tick_source(&tick);

    /* The diagnostics come across whether or not it succeeded, because they are
     * the reason this struct carries them: a caller reads them to find out WHICH
     * step failed, and a failure that reports nothing is the failure this file
     * is written to avoid. Copied field by field rather than nesting the struct,
     * so that no caller of lseref_measure_t changes in the commit that fixes the
     * counter. */
    out->apb4enr        = tick.apb4enr;
    out->bdcr           = tick.bdcr;
    out->prer           = tick.prer;
    out->icsr           = tick.icsr;
    out->rsf_polls      = tick.rsf_polls;
    out->ck_apre_hz     = tick.ck_apre_hz;
    out->bus_clock_on   = tick.bus_clock_on;
    out->clock_selected = tick.clock_selected;
    out->synchronised   = tick.synchronised;
    if (rc != 0) {
        return -1;
    }

    if (!board_cycles_available()) {
        return -1;   /* a count of zero looks like an infinitely fast core */
    }

    /* Gate on ticks, not on time. Start at a tick boundary so the first partial
     * tick is not counted, then count exactly `ticks` intervals. This is the
     * rule lseref.h now states for lseref_tick_mark's callers as well, because
     * it is the same rule and the counter got it wrong by not having it. */
    ssr = RTC_SSR & RTC_SSR_SS_MSK;
    if (!wait_ssr_change(&ssr)) {
        return -1;
    }
    t0 = board_cycles_now();
    for (n = 0u; n < ticks; n++) {
        if (!wait_ssr_change(&ssr)) {
            return -1;
        }
    }
    t1 = board_cycles_now();

    /* Unsigned subtraction, correct across one wrap of the 32-bit counter. At
     * 280 MHz the counter wraps every 15.3 seconds and this gate is one, so a
     * wrap is possible but never two. */
    out->cycles = t1 - t0;

    /* 64-bit, because cycles times the tick rate overflows 32 bits long before
     * the division brings it back. */
    {
        const uint64_t hz = ((uint64_t) out->cycles * (uint64_t) out->ck_apre_hz)
                          / (uint64_t) ticks;
        if (hz == 0u || hz > 0xFFFFFFFFu) {
            return -1;
        }
        out->core_hz_measured = (uint32_t) hz;
    }

    out->ok = true;
    return 0;
}

#else  /* !BOARD_REGS_CONFIRMED */

int lseref_start(lseref_start_t *out, uint32_t timeout_ms)
{
    (void) out;
    (void) timeout_ms;
    return -1;
}

int lseref_measure_core_hz(lseref_measure_t *out, uint32_t ticks)
{
    (void) out;
    (void) ticks;
    return -1;
}

int lseref_tick_open(lseref_tick_t *out)
{
    (void) out;
    return -1;
}

/* 0, which lseref.h says means nothing. A caller that reads this without having
 * had a 0 from lseref_tick_open would see a mark that never changes, which is
 * the correct description of a tick source that does not exist. */
uint32_t lseref_tick_mark(void)
{
    return 0u;
}

#endif /* BOARD_REGS_CONFIRMED */
