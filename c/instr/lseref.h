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
 * crystal at 32.768 kHz is good to a few tens of parts per million, two orders
 * better than the 0.36 per cent that separates 280 MHz from 279, and it needs no
 * instrument and no wiring.
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

#endif /* LSEREF_H */
