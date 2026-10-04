/* c/instr/lseref.h: the 32.768 kHz crystal, as a reference rather than a clock.
 *
 * WHAT THIS IS FOR. Every frequency in this volume is derived: an 8 MHz board
 * fact multiplied and divided by register fields that were read back. The
 * read-back proves the bits and not their meaning, and that is the only doubt
 * left about the 280 MHz claim. Reading `DIVM1` as 4 does not prove the field is
 * a plain divisor rather than something else that happens to hold 4.
 *
 * This crystal is the one reference on the board that does not come from the PLL
 * chain, so counting core cycles against it can settle the interpretation. A
 * crystal at 32.768 kHz is good to a few tens of parts per million by its
 * datasheet, two orders better than the 0.36 per cent that separates 280 MHz
 * from 279, and it needs no instrument and no wiring. The uncertainty actually
 * DEMONSTRATED on this board is weaker and is recorded at CORE_HZ_MEASURED in
 * stm32h7a3_regs.h: a few hundred parts per million, which is where this
 * crystal and a host PC's clock agree on the internal oscillator.
 *
 * THIS HEADER IS THE FIRST STEP ONLY, and the step is worth taking on its own:
 * make the crystal oscillate and say whether it did. That it is fitted has been
 * a settled board fact since Friday 2 October 2026, taken from two
 * machine-readable sources, and nothing in this repository had ever asked it to
 * run. The measurement that uses it comes after, and is not here.
 *
 * WHY IT MIGHT NOT START, which is the part a caller has to handle. A 32.768 kHz
 * crystal is a slow oscillator and its startup is measured in hundreds of
 * milliseconds, up to a second or two in the worst case the datasheets allow.
 * Anything that waits on it with the bound used for a PLL will conclude it is
 * broken. And the backup domain is write protected at reset, so the enable is
 * ignored rather than refused until that protection is lifted.
 */
#ifndef LSEREF_H
#define LSEREF_H

#include <stdbool.h>
#include <stdint.h>

/* What an attempt to start the crystal did. Every field is reported rather than
 * collapsed into a bool, because the interesting failures are distinguishable
 * only by these values: a backup domain still locked reads back BDCR unchanged,
 * a crystal that is absent or not loaded never sets LSERDY however long the
 * wait, and a crystal that was already running reports zero wait. */
typedef struct {
    uint32_t bdcr_before;     /* as found, BEFORE anything is written */
    uint32_t bdcr_after;
    bool     backup_unlocked; /* PWR_CR1's DBP read back as set */
    bool     already_running;  /* LSEON and LSERDY were both set on entry */
    bool     started;          /* LSERDY is set now */
    uint32_t waited_ms;        /* 0 when it was already running */
} lseref_start_t;

/* Lift the backup domain's write protection, enable the crystal, and wait up to
 * `timeout_ms` for it to report ready. Returns 0 when it is running.
 *
 * Writes nothing else in the backup domain. In particular it does not select or
 * enable the real-time clock: that is a separate decision with its own
 * consequences, and this function exists to answer one question.
 *
 * `timeout_ms` should be at least 2000. The millisecond granularity comes from
 * board_delay_ms, which is calibrated against the core clock, so a timeout is
 * only as accurate as the clock under test. That is acceptable for a bound and
 * is the reason the figure is reported as "waited" rather than as a measurement
 * of the startup time. */
int lseref_start(lseref_start_t *out, uint32_t timeout_ms);


/* ----------------------------------------------- the measurement itself
 *
 * What it does. Selects the crystal as the real-time clock's source, then counts
 * core cycles with DWT_CYCCNT over a gate of a whole number of sub second ticks.
 * The sub second register steps at the crystal divided by PREDIV_A plus one,
 * which with the reset prescaler is 256 Hz, so a gate of 256 ticks is one second
 * and the cycle count over it IS the core frequency in hertz.
 *
 * WHY THIS IS A MEASUREMENT AND THE REGISTER READ-BACK IS NOT. The 280 MHz claim
 * is an 8 MHz board fact multiplied and divided by fields that were read back,
 * and a read-back proves the bits rather than their meaning. This counts the core
 * clock against a reference that does not come from the PLL chain, so it tests
 * the interpretation. Over a one second gate the counting resolution is about
 * four parts per billion and the real floor is the crystal's own accuracy, a few
 * tens of parts per million by its datasheet, which is two orders better than
 * the 0.36 per cent separating 280 MHz from 279. What has since been
 * DEMONSTRATED on this board is weaker than that and is recorded at
 * CORE_HZ_MEASURED: the crystal agrees with a host PC's clock on the internal
 * oscillator to a few hundred parts per million, which is the honest
 * uncertainty here.
 *
 * WHAT IT STILL RESTS ON, because no measurement is free of assumptions:
 *   - the crystal is 32.768 kHz to its datasheet tolerance, which is the floor
 *     and is not measured here. Nothing on this board can measure it.
 *   - PREDIV_A is READ from PRER rather than assumed, so a non-default prescaler
 *     changes the gate rather than silently scaling the answer
 *   - DWT_CYCCNT counts core clock cycles, which is what makes this the CORE
 *     frequency and not the bus frequency
 *
 * It writes nothing inside the RTC. Only RCC's bus clock enable and the backup
 * domain's clock selection, so no write protection key is involved and the
 * calendar is never touched.
 */
typedef struct {
    bool     ok;
    uint32_t core_hz_measured;  /* 0 when it could not be measured */
    uint32_t core_hz_derived;   /* what board_core_hz() believes */
    int32_t  error_ppm;         /* measured against derived, 0 when either is 0 */

    uint32_t ck_apre_hz;        /* the sub second tick rate, computed from PRER */
    uint32_t ticks;             /* the gate, in sub second ticks */
    uint32_t cycles;            /* core cycles counted over the gate */

    uint32_t apb4enr;           /* read back, to show the bus clock is on */
    uint32_t bdcr;              /* read back, to show which clock is selected */
    uint32_t prer;              /* read back, because the gate depends on it */
    uint32_t icsr;              /* read back, for the synchronisation flag */
    uint32_t rsf_polls;         /* how long the shadow took to report valid */
    bool     bus_clock_on;
    bool     clock_selected;
    bool     synchronised;      /* the shadow registers reported valid */
} lseref_measure_t;

/* Measure the core frequency against the crystal. `ticks` is the gate in sub
 * second ticks; 256 is one second with the reset prescaler and is the sensible
 * default. Returns 0 on success.
 *
 * Requires lseref_start() to have succeeded first, and board_cycles_available()
 * to be true: without the cycle counter there is nothing to count with, and a
 * count of zero looks exactly like an infinitely fast core.
 *
 * Takes `ticks` divided by the tick rate of wall time, so about a second at the
 * default. Every wait inside is bounded. */
int lseref_measure_core_hz(lseref_measure_t *out, uint32_t ticks);

#endif /* LSEREF_H */
