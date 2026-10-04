/* P01's second image: raise this part to 280 MHz, and report every step.
 *
 * `p01-first-light` proves the toolchain, the linker script, the vector table
 * and the reset handler on the internal oscillator, and refuses to go further.
 * This image is the going further, and it is a separate target rather than a
 * change to that one for a reason worth stating: every cycle figure published in
 * this volume was measured at 64 MHz, and an image that silently raised the
 * clock would invalidate all of them while changing no line of their code.
 *
 * WHAT IT PRINTS BEFORE IT CHANGES ANYTHING, and why that order is deliberate.
 * The first thing on the console is the reset value of the five registers the
 * sequence is about to write. If the sequence then hangs or faults, that dump is
 * already out of the part and in the terminal, which is the difference between a
 * failure somebody can read and a board that stopped talking. It is also the
 * only way to see a wrong peripheral base address: a register that reads as zero
 * where the manual says it has a reset value is not a register.
 *
 * WHAT IT DOES NOT CLAIM. 280 MHz here is DERIVED and not MEASURED. The number
 * comes from an 8 MHz board fact multiplied and divided by fields read back out
 * of the registers, and every one of those reads could be right while the clock
 * is something else. Three things in this image are evidence, and the README says
 * what each is worth:
 *
 *   - every step's register read back the value intended, which rules out a
 *     wrong address and a field that moved between parts
 *   - the console stays readable after the switch, which rules out an error of
 *     a factor in the peripheral clock, because the divider is recomputed from
 *     the new frequency and the host's own UART is clocked by the host
 *   - LD1 blinks at one second per cycle by the part's own count, which a reader
 *     can check against a watch, and which rules out the same factor errors by
 *     a cruder and completely independent route
 *
 * None of the three can tell 280 MHz from 279. That needs an instrument that
 * does not share this clock, which is what P06's witness is for and what
 * freqcount.c still refuses to be.
 */
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "stm32h7a3_regs.h"

extern void board_init(void);

/* The registers the sequence touches, dumped by name so the console line and the
 * reference manual use the same word. Printed before and after, because a
 * before-and-after of the same five registers is the whole experiment. */
static void dump_registers(const char *when)
{
    printf("  %s:\n", when);
#ifdef BOARD_REGS_CONFIRMED
    /* CR3 first, because it is the one that gates everything after it. Both
     * SMPSEN and LDOEN set is Run* mode: no supply chosen, and no voltage scale
     * change will be acknowledged. */
    printf("    PWR_CR3       %08lX   BYPASS %lu  LDOEN %lu  SMPSEN %lu\n",
           (unsigned long) PWR_CR3,
           (unsigned long) ((PWR_CR3 & PWR_CR3_BYPASS_MSK) ? 1u : 0u),
           (unsigned long) ((PWR_CR3 & PWR_CR3_LDOEN_MSK) ? 1u : 0u),
           (unsigned long) ((PWR_CR3 & PWR_CR3_SMPSEN_MSK) ? 1u : 0u));
    printf("    PWR_SRDCR     %08lX   VOS %lu  VOSRDY %lu\n",
           (unsigned long) PWR_SRDCR,
           (unsigned long) ((PWR_SRDCR & PWR_SRDCR_VOS_MSK) >> PWR_SRDCR_VOS_POS),
           (unsigned long) ((PWR_SRDCR & PWR_SRDCR_VOSRDY_MSK) ? 1u : 0u));
    /* The scale ACTUALLY in use, beside the one selected above. A selected
     * value and an active value that disagree is a different fault from a write
     * that did not land, and this is the only field that separates them. */
    printf("    PWR_CSR1      %08lX   ACTVOS %lu  ACTVOSRDY %lu\n",
           (unsigned long) PWR_CSR1,
           (unsigned long) ((PWR_CSR1 & PWR_CSR1_ACTVOS_MSK) >> PWR_CSR1_ACTVOS_POS),
           (unsigned long) ((PWR_CSR1 & PWR_CSR1_ACTVOSRDY_MSK) ? 1u : 0u));
    printf("    FLASH_ACR     %08lX   latency %lu wait states\n",
           (unsigned long) FLASH_ACR,
           (unsigned long) ((FLASH_ACR & FLASH_ACR_LATENCY_MSK)
                            >> FLASH_ACR_LATENCY_POS));
    printf("    RCC_CR        %08lX   HSEBYP %lu  HSERDY %lu  PLL1RDY %lu\n",
           (unsigned long) RCC_CR,
           (unsigned long) ((RCC_CR & RCC_CR_HSEBYP_MSK) ? 1u : 0u),
           (unsigned long) ((RCC_CR & RCC_CR_HSERDY_MSK) ? 1u : 0u),
           (unsigned long) ((RCC_CR & RCC_CR_PLL1RDY_MSK) ? 1u : 0u));
    printf("    RCC_PLLCKSELR %08lX   source %lu  DIVM1 %lu\n",
           (unsigned long) RCC_PLLCKSELR,
           (unsigned long) (RCC_PLLCKSELR & RCC_PLLCKSELR_PLLSRC_MSK),
           (unsigned long) ((RCC_PLLCKSELR & RCC_PLLCKSELR_DIVM1_MSK)
                            >> RCC_PLLCKSELR_DIVM1_POS));
    printf("    RCC_PLLCFGR   %08lX   RGE %lu  VCOSEL %lu  FRACEN %lu  DIVP1EN %lu\n",
           (unsigned long) RCC_PLLCFGR,
           (unsigned long) ((RCC_PLLCFGR & RCC_PLLCFGR_PLL1RGE_MSK)
                            >> RCC_PLLCFGR_PLL1RGE_POS),
           (unsigned long) ((RCC_PLLCFGR & RCC_PLLCFGR_PLL1VCOSEL_MSK) ? 1u : 0u),
           (unsigned long) ((RCC_PLLCFGR & RCC_PLLCFGR_PLL1FRACEN_MSK) ? 1u : 0u),
           (unsigned long) ((RCC_PLLCFGR & RCC_PLLCFGR_DIVP1EN_MSK) ? 1u : 0u));
    /* N and P are printed as the VALUES, with the raw field beside them, because
     * the register holds each minus one and a reader comparing the console
     * against ST's example needs to see both or will think it is off by one. */
    printf("    RCC_PLL1DIVR  %08lX   N %lu (field %lu)  P %lu (field %lu)\n",
           (unsigned long) RCC_PLL1DIVR,
           (unsigned long) (((RCC_PLL1DIVR & RCC_PLL1DIVR_N1_MSK)
                             >> RCC_PLL1DIVR_N1_POS) + 1u),
           (unsigned long) ((RCC_PLL1DIVR & RCC_PLL1DIVR_N1_MSK)
                            >> RCC_PLL1DIVR_N1_POS),
           (unsigned long) (((RCC_PLL1DIVR & RCC_PLL1DIVR_P1_MSK)
                             >> RCC_PLL1DIVR_P1_POS) + 1u),
           (unsigned long) ((RCC_PLL1DIVR & RCC_PLL1DIVR_P1_MSK)
                            >> RCC_PLL1DIVR_P1_POS));
    printf("    RCC_CFGR      %08lX   SW %lu  SWS %lu\n",
           (unsigned long) RCC_CFGR,
           (unsigned long) (RCC_CFGR & RCC_CFGR_SW_MSK),
           (unsigned long) ((RCC_CFGR & RCC_CFGR_SWS_MSK) >> RCC_CFGR_SWS_POS));
    printf("    RCC_CDCFGR1   %08lX   CDCPRE %lu  HPRE %lu\n",
           (unsigned long) RCC_CDCFGR1,
           (unsigned long) ((RCC_CDCFGR1 & RCC_CDCFGR1_CDCPRE_MSK)
                            >> RCC_CDCFGR1_CDCPRE_POS),
           (unsigned long) ((RCC_CDCFGR1 & RCC_CDCFGR1_HPRE_MSK)
                            >> RCC_CDCFGR1_HPRE_POS));
#else
    printf("    the registers are not confirmed, so nothing is read\n");
#endif
}

static const char *status_word(board_status_t s)
{
    switch (s) {
    case BOARD_OK:                   return "at the target";
    case BOARD_CLOCK_AT_RESET_SPEED: return "known, but the reset clock";
    default:                         return "not established";
    }
}

static void report_clock(const char *when)
{
    printf("  %s: core %lu Hz, APB1 %lu Hz, %s\n", when,
           (unsigned long) board_core_hz(),
           (unsigned long) board_pclk1_hz(),
           status_word(board_clock_status()));
}

int main(void)
{
    clock280_result_t result;
    unsigned i;
    int rc;

    board_init();

    printf("\nP01, the clock tree. Raising this part to 280 MHz.\n");
    printf("registers from: %s\n", BOARD_REGS_CONFIRMED);
    printf("the 280 MHz tree from: %s\n\n", BOARD_CLOCK_280_SOURCE);

    report_clock("before");
    dump_registers("the registers at reset");

    /* The one thing worth saying before the attempt: what the sequence intends,
     * in the arithmetic a reader can check, so the console carries the claim and
     * not only the outcome. */
    printf("\n  intending: the SMPS selected first, by clearing LDOEN, to exit\n");
    printf("             Run* mode. At reset both enables are set, which is no\n");
    printf("             selection at all, and the regulator then refuses every\n");
    printf("             voltage scale change in silence,\n");
    printf("             then voltage scale 1, then scale 0, because scale 0\n");
    printf("             is only reachable from scale 1 on this part,\n");
    printf("             then 8 MHz bypass / DIVM1 4 = 2 MHz into the PLL,\n");
    printf("             times N 280 = 560 MHz oscillator, / P 2 = 280 MHz sys_ck,\n");
    printf("             CDCPRE 1 so the core is 280 MHz, HPRE 2 so every bus is 140\n\n");

    rc = board_clock_raise_to_280(&result);

    /* The console's divider came from the old APB1 frequency, so it has to be
     * recomputed before anything else is printed. Done unconditionally: on a
     * failure the clock did not change and re-initialising is harmless, and
     * making it conditional would mean one of the two paths was never exercised.
     *
     * board_clock_rescan() already ran inside the sequence on success. Calling
     * it again here costs a few register reads and means this image reports the
     * registers as they are now, whichever way the attempt went. */
    board_clock_rescan();
    board_console_init();

    printf("\n");
    for (i = 0; i < (unsigned) CLOCK280_STEP_COUNT; i++) {
        const clock280_record_t *r = &result.step[i];
        if (!r->attempted) {
            printf("  step %u  not reached   %s\n", i + 1u,
                   board_clock280_step_name((clock280_step_t) i));
            continue;
        }
        printf("  step %u  %s  %s\n", i + 1u, r->ok ? "ok        " : "REFUSED   ",
               board_clock280_step_name((clock280_step_t) i));
        printf("            wrote %08lX  read back %08lX  spins %lu\n",
               (unsigned long) r->wrote, (unsigned long) r->read_back,
               (unsigned long) r->spins);
    }

    printf("\n");
    dump_registers("the registers now");
    report_clock("after");

    if (rc != 0) {
        printf("\n  THE CLOCK WAS NOT RAISED, and the part is still on the "
               "internal\n  oscillator at 64 MHz with this console working, "
               "which is the\n  designed outcome of a failure rather than luck. "
               "The step marked\n  REFUSED above is where to look, and its two "
               "register values say\n  which kind of wrong it is: a read back of "
               "zero where a value was\n  written is usually an address, and a "
               "read back of something else\n  is usually a field that moved "
               "between this part and the H743.\n");
        if (result.failed_at < CLOCK280_STEP_COUNT) {
            printf("  it stopped at: %s\n",
                   board_clock280_step_name(result.failed_at));
        }
    } else {
        printf("\n  Every step read back what was written and the decoder agrees "
               "with\n  the target. THIS IS STILL NOT A MEASUREMENT. 280 MHz "
               "here is 8 MHz\n  of board fact multiplied and divided by fields "
               "read out of these\n  registers. What makes it a measurement is an "
               "instrument that does\n  not share this clock, and this repository "
               "does not have one wired\n  up yet: freqcount.c refuses, because "
               "the timer registers it needs\n  are not confirmed.\n");
        printf("\n  LD1 green on PB0 now blinks at one second per cycle by this\n"
               "  part's own count. Against a watch, that is the crudest check\n"
               "  available and it still rules out an error of a factor, which is\n"
               "  the error class a wrong PLL field produces.\n");
    }

    /* And then blink, so the board says something a person across the room can
     * read. One second per cycle, from board_delay_ms, which was calibrated
     * against the cycle counter at whatever the clock is now. */
    board_delay_calibrate();
    printf("\n  delay loop: %lu iterations per millisecond\n\n",
           (unsigned long) board_delay_iters_per_ms());

    for (;;) {
        board_led_toggle(BOARD_LED_GREEN);
        (void) board_delay_ms(500u);
    }
}
