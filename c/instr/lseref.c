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

#else  /* !BOARD_REGS_CONFIRMED */

int lseref_start(lseref_start_t *out, uint32_t timeout_ms)
{
    (void) out;
    (void) timeout_ms;
    return -1;
}

#endif /* BOARD_REGS_CONFIRMED */
