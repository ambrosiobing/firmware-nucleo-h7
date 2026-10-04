/* c/board/clock280.c: raising this part to 280 MHz, one gated step at a time.
 *
 * P01 reached first light on the internal oscillator at 64 MHz and refused to
 * go further, because the register sequence for 280 MHz rested on values this
 * repository had not sourced. Those values were sourced on Sunday 4 October
 * 2026 and are recorded at BOARD_CLOCK_280_SOURCE in stm32h7a3_regs.h, with the
 * two traps that would have caught a confident guess written beside the fields
 * they apply to. This file is the sequence they make possible.
 *
 * WHY THE ORDER IS THE ORDER, and why that makes the whole thing safe to try on
 * a running board. Every intermediate state this function passes through is a
 * state the part can run at 64 MHz:
 *
 *   1  voltage scaling UP to scale 1, from whatever the reset scale is.
 *   2  voltage scaling UP again, scale 1 to scale 0. TWO steps because scale 0
 *      is only reachable from scale 1, which is a constraint on the transition
 *      rather than on the value and which this board established by refusing a
 *      direct write. More voltage than 64 MHz needs is not a hazard; less than
 *      280 MHz needs is.
 *   3  flash latency UP, to six wait states. Too many wait states is slow and
 *      correct. Too few returns garbage from flash, which at this point in the
 *      sequence would mean the next instruction fetched.
 *   4  the external 8 MHz clock on, which nothing yet depends on.
 *   5  the PLL configured and locked, while the core still runs on HSI.
 *   6  the bus prescalers set, while sys_ck is still HSI, so the buses divide
 *      down from 64 MHz for a moment and nothing is out of range.
 *   7  the switch, which is the only irreversible-feeling step and the only one
 *      taken after every prerequisite has been read back.
 *
 * Reversing any pair of those puts the part somewhere it cannot run. Raising the
 * clock before the latency is the classic one: the first flash read after the
 * switch returns rubbish and the symptom is a hard fault with no clue in it.
 *
 * NOTHING HERE SPINS FOREVER. ST's own example waits on VOSRDY with
 * `while(!flag){}`, which on a board where that bit never sets is
 * indistinguishable from a dead image. Every wait here is bounded, and running
 * out is a reported failure with the register's value attached, because this
 * repository's whole method is that an experiment must be able to fail in a way
 * somebody can read.
 *
 * AND IT GIVES UP RATHER THAN PRESSING ON. If any step fails, the function
 * returns without switching sys_ck, which leaves the part on HSI at 64 MHz with
 * a working console, so the failure can be read off COM13 rather than guessed at
 * from a board that no longer talks.
 */
#include "board.h"
#include "stm32h7a3_regs.h"

#include <stddef.h>

#ifdef BOARD_REGS_CONFIRMED

/* Bounded, and generous. At 64 MHz each iteration is a few cycles, so this is
 * tens of milliseconds, against ready flags that settle in microseconds. The
 * number is a bound rather than a timeout: nothing here needs it to be precise,
 * and it must not be so tight that a slow start reads as a failure. */
#define READY_SPINS_MAX  1000000u

/* The bus prescalers this file sets, and the reason they are not ST's.
 *
 * ST's example for this board sets HPRE to divide by one, so the AHB runs at the
 * full 280 MHz, and then divides each of APB1, APB2, APB3 and APB4 by two to
 * bring them to 140 MHz. That needs four prescaler fields. This repository has
 * confirmed one of them, CDPPRE1.
 *
 * Leaving the other three at their reset value of divide by one while sys_ck is
 * 280 MHz would run three buses at 280 MHz against a limit of 140, and nothing
 * in the part would refuse: it would work until it did not. So this file takes
 * the other route to the same core frequency. CDCPRE divides by one, so the CPU
 * gets the full 280 MHz, and HPRE divides by two, so the AHB and therefore every
 * APB below it is at 140 MHz whatever its own prescaler says.
 *
 * WHAT THAT COSTS, because it is not free. The AHB runs at half the frequency
 * ST's configuration gives it, which halves the bandwidth to flash and to the
 * AXI SRAMs. For a chapter whose subject is the clock tree that is a fair price
 * for using only fields with a named source. The condition for changing it is
 * explicit: read CDPPRE2 and the APB3 and APB4 prescaler fields from the same
 * header, add them beside CDPPRE1, and then HPRE can go to divide by one.
 */
#define CLOCK280_HPRE    RCC_AHBPRE_DIV2
#define CLOCK280_CDCPRE  RCC_AHBPRE_DIV1

static const char *const step_names[CLOCK280_STEP_COUNT] = {
    "voltage scaling, the reset scale to scale 1",
    "voltage scaling, scale 1 to scale 0",
    "flash latency to six wait states",
    "the external 8 MHz clock in bypass",
    "the PLL locked at 560 MHz",
    "the bus prescalers",
    "the switch of sys_ck to the PLL",
};


const char *board_clock280_step_name(clock280_step_t step)
{
    if ((unsigned) step >= (unsigned) CLOCK280_STEP_COUNT) {
        return "an unknown step";
    }
    return step_names[step];
}

/* Wait for a mask to reach a wanted state, bounded. Returns the number of
 * iterations taken, or READY_SPINS_MAX when it ran out, and the caller compares
 * against that rather than being handed a bare bool: how long a flag took is
 * worth printing even when it arrived. */
static uint32_t wait_for(volatile uint32_t *reg, uint32_t mask, bool want_set)
{
    uint32_t spins = 0u;

    while (spins < READY_SPINS_MAX) {
        const bool is_set = ((*reg & mask) != 0u);
        if (is_set == want_set) {
            return spins;
        }
        spins++;
    }
    return READY_SPINS_MAX;
}

static void record(clock280_record_t *r, uint32_t wrote, uint32_t read_back,
                   uint32_t spins, bool ok)
{
    r->attempted = true;
    r->wrote     = wrote;
    r->read_back = read_back;
    r->spins     = spins;
    r->ok        = ok;
}

/* One voltage scaling transition, requested and then waited for.
 *
 * SCALE 0 IS ONLY REACHABLE FROM SCALE 1. That is a property of the transition
 * rather than of the value, it is in ST's doc comment and not in the register
 * description, and the board taught it on Sunday 4 October 2026 by reading VOS
 * back as 3 while leaving VOSRDY clear for a million polls. So this is called
 * twice and the two calls are separate reported steps.
 *
 * The read before the poll is deliberate and is what ST's own macro does: it
 * reads the field back to be sure the write has landed before anything looks at
 * the ready flag. */
static bool vos_step(clock280_record_t *r, uint32_t scale)
{
    const uint32_t want = scale << PWR_SRDCR_VOS_POS;
    uint32_t spins;
    uint32_t got;

    PWR_SRDCR = (PWR_SRDCR & ~PWR_SRDCR_VOS_MSK) | want;
    got = PWR_SRDCR;                 /* land the write before polling */
    spins = wait_for(&PWR_SRDCR, PWR_SRDCR_VOSRDY_MSK, true);

    got = PWR_SRDCR;
    const bool ok = ((got & PWR_SRDCR_VOS_MSK) == want)
                 && (spins < READY_SPINS_MAX);
    record(r, want, got, spins, ok);
    return ok;
}

int board_clock_raise_to_280(clock280_result_t *out)
{
    unsigned i;

    if (out == NULL) {
        return -1;
    }
    for (i = 0; i < (unsigned) CLOCK280_STEP_COUNT; i++) {
        out->step[i].attempted = false;
        out->step[i].ok        = false;
        out->step[i].wrote     = 0u;
        out->step[i].read_back = 0u;
        out->step[i].spins     = 0u;
    }
    out->failed_at   = CLOCK280_STEP_COUNT;
    out->reached_280 = false;

    /* ---- 1 and 2. voltage scaling, up, through scale 1 ------------------- */
    if (!vos_step(&out->step[CLOCK280_STEP_VOS1], PWR_VOS_SCALE1)) {
        out->failed_at = CLOCK280_STEP_VOS1;
        return -1;
    }
    if (!vos_step(&out->step[CLOCK280_STEP_VOS0], PWR_VOS_SCALE0)) {
        out->failed_at = CLOCK280_STEP_VOS0;
        return -1;
    }

    /* ---- 3. flash latency, up -------------------------------------------- */
    {
        clock280_record_t *r = &out->step[CLOCK280_STEP_LATENCY];
        const uint32_t want = FLASH_LATENCY_280MHZ << FLASH_ACR_LATENCY_POS;

        FLASH_ACR = (FLASH_ACR & ~FLASH_ACR_LATENCY_MSK) | want;

        /* Read back immediately and compare. There is no ready flag for this
         * one, and the read-back is the only evidence the write took: a wrong
         * peripheral base address writes into nothing and reads back the reset
         * value, which is exactly what this comparison catches. */
        const uint32_t got = FLASH_ACR;
        const bool ok = ((got & FLASH_ACR_LATENCY_MSK) == want);
        record(r, want, got, 0u, ok);
        if (!ok) {
            out->failed_at = CLOCK280_STEP_LATENCY;
            return -1;
        }
    }

    /* ---- 4. the external clock ------------------------------------------- */
    {
        clock280_record_t *r = &out->step[CLOCK280_STEP_HSE];
        uint32_t spins;

        /* Bypass BEFORE enable. This board has no crystal: the on-board
         * debugger drives the pin. Enabling HSE without the bypass bit waits
         * for an oscillator that is not fitted, and the bound above is what
         * turns that from a hang into a report. */
        RCC_CR |= RCC_CR_HSEBYP_MSK;
        RCC_CR |= RCC_CR_HSEON_MSK;
        spins = wait_for(&RCC_CR, RCC_CR_HSERDY_MSK, true);

        const uint32_t got = RCC_CR;
        const bool ok = ((got & RCC_CR_HSEBYP_MSK) != 0u)
                     && ((got & RCC_CR_HSERDY_MSK) != 0u)
                     && (spins < READY_SPINS_MAX);
        record(r, RCC_CR_HSEBYP_MSK | RCC_CR_HSEON_MSK, got, spins, ok);
        if (!ok) {
            out->failed_at = CLOCK280_STEP_HSE;
            return -1;
        }
    }

    /* ---- 5. the PLL ------------------------------------------------------ */
    {
        clock280_record_t *r = &out->step[CLOCK280_STEP_PLL];
        uint32_t spins;

        /* Off first. The source and divider fields cannot be written while the
         * PLL runs, and at reset it is already off, so this is defensive rather
         * than necessary. Defensive is right: this function is also what a
         * second call would run. */
        RCC_CR &= ~RCC_CR_PLL1ON_MSK;
        spins = wait_for(&RCC_CR, RCC_CR_PLL1RDY_MSK, false);
        if (spins >= READY_SPINS_MAX) {
            record(r, 0u, RCC_CR, spins, false);
            out->failed_at = CLOCK280_STEP_PLL;
            return -1;
        }

        /* Source and the input divider. DIVM1 holds the value itself. */
        RCC_PLLCKSELR = (RCC_PLLCKSELR
                         & ~(RCC_PLLCKSELR_PLLSRC_MSK | RCC_PLLCKSELR_DIVM1_MSK))
                      | RCC_PLLSRC_HSE
                      | (4u << RCC_PLLCKSELR_DIVM1_POS);

        /* The ranges, the fractional divider off, and P1 enabled as an output.
         * FRACEN is cleared explicitly: a fractional term left enabled would
         * move the frequency by an amount this file does not decode, and
         * board_pll1_hz() in system.c refuses rather than decode it.
         *
         * VCOSEL is not ORed in because the wide range IS the cleared state:
         * RCC_PLL1VCOWIDE is 0 in the HAL and the mask above clears the bit.
         * Writing `RCC_PLL1VCO_WIDE << 1` would have put a bare 1 in this file
         * for a bit position, which is the kind of constant that survives a
         * later edit to a field that has moved. */
        RCC_PLLCFGR = (RCC_PLLCFGR
                       & ~(RCC_PLLCFGR_PLL1FRACEN_MSK | RCC_PLLCFGR_PLL1VCOSEL_MSK
                           | RCC_PLLCFGR_PLL1RGE_MSK))
                    | (RCC_PLL1VCI_2_TO_4_MHZ << RCC_PLLCFGR_PLL1RGE_POS)
                    | RCC_PLLCFGR_DIVP1EN_MSK;

        /* N, P, Q and R, every one of them the value MINUS ONE. See the note at
         * RCC_PLL1DIVR_N1_POS: DIVM1 above is not, and that asymmetry is
         * sourced rather than assumed. Q and R are set to a legal value rather
         * than left alone, because this file does not use them and a reset
         * value is not obviously legal for a 560 MHz oscillator. */
        const uint32_t divr = ((280u - 1u) << RCC_PLL1DIVR_N1_POS)
                            | ((2u - 1u)   << RCC_PLL1DIVR_P1_POS)
                            | ((2u - 1u)   << RCC_PLL1DIVR_Q1_POS)
                            | ((2u - 1u)   << RCC_PLL1DIVR_R1_POS);
        RCC_PLL1DIVR = divr;

        RCC_CR |= RCC_CR_PLL1ON_MSK;
        spins = wait_for(&RCC_CR, RCC_CR_PLL1RDY_MSK, true);

        const uint32_t got = RCC_PLL1DIVR;
        const bool ok = ((RCC_CR & RCC_CR_PLL1RDY_MSK) != 0u)
                     && (spins < READY_SPINS_MAX)
                     && (got == divr);
        record(r, divr, got, spins, ok);
        if (!ok) {
            out->failed_at = CLOCK280_STEP_PLL;
            return -1;
        }
    }

    /* ---- 6. the bus prescalers, while sys_ck is still 64 MHz ------------- */
    {
        clock280_record_t *r = &out->step[CLOCK280_STEP_BUSES];
        const uint32_t want = (CLOCK280_CDCPRE << RCC_CDCFGR1_CDCPRE_POS)
                            | (CLOCK280_HPRE   << RCC_CDCFGR1_HPRE_POS);

        RCC_CDCFGR1 = (RCC_CDCFGR1
                       & ~(RCC_CDCFGR1_CDCPRE_MSK | RCC_CDCFGR1_HPRE_MSK))
                    | want;

        const uint32_t got = RCC_CDCFGR1;
        const bool ok = ((got & (RCC_CDCFGR1_CDCPRE_MSK | RCC_CDCFGR1_HPRE_MSK))
                         == want);
        record(r, want, got, 0u, ok);
        if (!ok) {
            out->failed_at = CLOCK280_STEP_BUSES;
            return -1;
        }
    }

    /* ---- 7. the switch --------------------------------------------------- */
    {
        clock280_record_t *r = &out->step[CLOCK280_STEP_SWITCH];
        uint32_t spins = 0u;

        RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_SW_MSK)
                 | (RCC_SW_PLL1 << RCC_CFGR_SW_POS);

        /* SWS reports what the hardware actually switched to, which is a
         * different field from SW at a different position. Comparing SW with
         * itself would prove only that a write took. */
        while (spins < READY_SPINS_MAX) {
            if (((RCC_CFGR & RCC_CFGR_SWS_MSK) >> RCC_CFGR_SWS_POS)
                == RCC_SWS_PLL1) {
                break;
            }
            spins++;
        }

        const uint32_t got = RCC_CFGR;
        const bool ok = (((got & RCC_CFGR_SWS_MSK) >> RCC_CFGR_SWS_POS)
                         == RCC_SWS_PLL1)
                     && (spins < READY_SPINS_MAX);
        record(r, RCC_SW_PLL1 << RCC_CFGR_SW_POS, got, spins, ok);
        if (!ok) {
            out->failed_at = CLOCK280_STEP_SWITCH;
            return -1;
        }
    }

    /* Re-decode from the registers rather than asserting the target. If the
     * decode disagrees with 280 MHz after every step read back correctly, that
     * is a real finding about the decoder or about this file, and it is better
     * reported than papered over. */
    board_clock_rescan();
    out->reached_280 = (board_core_hz() == CORE_HZ_TARGET);
    return out->reached_280 ? 0 : -1;
}

#else  /* !BOARD_REGS_CONFIRMED */

const char *board_clock280_step_name(clock280_step_t step)
{
    (void) step;
    return "the registers are not confirmed";
}

int board_clock_raise_to_280(clock280_result_t *out)
{
    (void) out;
    return -1;
}

#endif /* BOARD_REGS_CONFIRMED */
