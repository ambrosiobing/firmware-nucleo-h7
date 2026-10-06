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
 * The first thing on the console is every register the sequence is about to
 * write, as found. If the sequence then hangs or faults, that dump is already
 * out of the part and in the terminal, which is the difference between a failure
 * somebody can read and a board that stopped talking. It is also the only way to
 * see a wrong peripheral base address: a register that reads as zero where the
 * manual says it has a reset value is not a register.
 *
 * AND IT MEASURES THE CLOCK, which it did not when it was first written. That
 * paragraph used to say 280 MHz was derived and not measured, name three weaker
 * pieces of evidence, and point at freqcount.c as the thing that would settle
 * it. All three of those are now out of date and the corrections matter:
 *
 *   - the clock IS measured, against this board's 32.768 kHz crystal, which is
 *     the one reference that does not come from the PLL chain. Not freqcount.c,
 *     which counts an external signal against the internal clock and therefore
 *     cannot witness the internal clock at all.
 *   - the measurement is taken TWICE, on the reset clock and at the new one. The
 *     second is the interesting number and the first is what makes it credible,
 *     because the reset oscillator had already been measured by two other
 *     instruments and the crystal has to agree with them.
 *   - the blink is NOT independent evidence, which the old comment claimed. The
 *     delay loop calibrates against DWT_CYCCNT and that counts the clock under
 *     test. It would catch an error of a factor and nothing finer.
 *
 * WHAT THE MEASUREMENT FOUND, on Sunday 4 October 2026: the core runs at
 * 279672822 Hz at the 280 MHz setting, 1168 parts per million low, because the
 * debugger's clock output is 7990652 Hz and not 8 MHz. The registers were all
 * correct and the arithmetic was all correct; the assumption nobody had written
 * down was the input frequency. Two further runs measured 279435368 and
 * 279714764 Hz, spanning 1000 parts per million with no trend, so each figure
 * is an observation of an unstable clock and not a constant. See the measured
 * clock table beside CORE_HZ_TARGET in stm32h7a3_regs.h and
 * P01's README.
 *
 * The readable console remains the other external check and is worth keeping:
 * the baud divider is recomputed from the new bus frequency and the host's own
 * serial port is clocked by the host, so clean text bounds the bus clock to
 * about two per cent. Coarse beside the crystal, and it needs nothing.
 */
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "clocktree.h"
#include "freqcount.h"
#include "lseref.h"
#include "pwmmath.h"
#include "pwmsrc.h"
#include "stm32h7a3_regs.h"

extern void board_init(void);

/* The registers the sequence touches, dumped by name so the console line and the
 * reference manual use the same word. Printed before and after, because a
 * before-and-after of the same registers is the whole experiment.
 *
 * "AS FOUND" AND NOT "AT RESET", and the difference is a finding rather than
 * pedantry. On Sunday 4 October 2026 this image was run twice, once after a
 * power cycle and once after the black RESET button, and three fields differed:
 *
 *     after a power cycle   PWR_CR3 00000006  LDOEN 1 SMPSEN 1, ACTVOSRDY 0,
 *                           HSEBYP 0, and step 1 took 31 polls
 *     after the RESET pin   PWR_CR3 00010004  LDOEN 0 SMPSEN 1, ACTVOSRDY 1,
 *                           HSEBYP 1, and step 1 took 0 polls
 *
 * The supply selection, the regulator's ready state and the external clock's
 * bypass bit all SURVIVE a system reset. Only removing power restores them. So a
 * reader comparing this dump against a reset-value column in a manual will find
 * disagreements that are not faults, and the heading has to say so.
 *
 * It also means the supply step is safely idempotent: writing the selection it
 * already holds is a no-op that still passes its own read-back gate, which
 * matches ST's HAL_PWREx_ConfigSupply having a branch for exactly that case. */
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

    /* ADDED SUNDAY 4 OCTOBER 2026, because this dump had a gap. The sequence
     * never writes CDCFGR2, so it was never printed, but the decode READS it for
     * the APB1 prescaler. That made the dump useless for the one thing
     * clock_vectors.json wants from it, a row of this board's own register
     * words: six of the seven were here, and six of seven is none. */
    printf("    RCC_CDCFGR2   %08lX   CDPPRE1 %lu\n",
           (unsigned long) RCC_CDCFGR2,
           (unsigned long) ((RCC_CDCFGR2 & RCC_CDCFGR2_CDPPRE1_MSK)
                            >> RCC_CDCFGR2_CDPPRE1_POS));
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

    /* The reason, printed only when there is one. board_clock_status() has a
     * single error value and the decode has eight ways to reach it, so a dead
     * clock used to send a reader to eight places at once. Silent on success,
     * because "ok" on every line is how a line stops being read. */
    if (board_clock_status() == BOARD_ERR_CLOCK_UNCONFIRMED) {
        printf("    the decode refused: %s\n", board_clock_refusal_text());
    }
}

/* The crystal, and the core clock counted against it.
 *
 * CALLED TWICE, before and after the clock change, and the second call is not
 * the interesting one. The first measurement of 280 MHz against this crystal on
 * Sunday 4 October 2026 came out 1168 parts per million low, which is forty
 * times the crystal's expected accuracy and a third of what one wrong digit in
 * the PLL's N field would give. So one of the two references is off by about
 * 0.117 per cent and the measurement alone cannot say which.
 *
 * The reset clock is what discriminates. P01 measured the internal oscillator at
 * 64.17 to 64.18 MHz across six reductions on two instruments against a host
 * PC's clock, a spread of 0.031 per cent. A crystal-referenced reading of that
 * same oscillator either lands in that band, which clears the crystal and makes
 * the 280 MHz figure a real property of the debugger's 8 MHz, or lands near
 * 64.095 MHz, which is what a crystal 1171 parts per million fast would produce
 * and which is outside the band six reductions established.
 *
 * Either answer is worth having and neither is assumed here. */
static void report_crystal(const char *when, uint32_t nominal_hz)
{
    lseref_start_t lse;
    lseref_measure_t m;
    int lse_rc;
    int m_rc;

    printf("\n  the 32.768 kHz crystal, %s. It is the only reference on this\n"
           "  board that does not come from the PLL chain:\n", when);

    lse_rc = lseref_start(&lse, 3000u);
    printf("    RCC_BDCR as found %08lX, after %08lX, backup domain %s\n",
           (unsigned long) lse.bdcr_before, (unsigned long) lse.bdcr_after,
           lse.backup_unlocked ? "unlocked" : "STILL LOCKED, enable ignored");
    if (lse.already_running) {
        printf("    already running, which is what a system reset leaves "
               "behind. The\n    backup domain is not reset by the RESET pin, "
               "so a zero wait here\n    means nothing was asked of it. Power "
               "cycle for a startup time.\n");
    } else if (lse_rc == 0) {
        printf("    started, after about %lu ms of waiting\n",
               (unsigned long) lse.waited_ms);
    } else {
        printf("    DID NOT START within 3000 ms. Nothing else depends on it.\n");
        return;
    }

    m_rc = lseref_measure_core_hz(&m, 256u);
    if (m_rc != 0) {
        printf("    the measurement refused: APB4ENR %08lX  BDCR %08lX  "
               "ICSR %08lX\n    PRER %08lX, tick %lu Hz. A bus clock of zero "
               "means RTCAPBEN did\n    not take; RSF never set means the clock "
               "selection never reached\n    the RTC; a tick of zero means "
               "PREDIV_A is unusable.\n",
               (unsigned long) m.apb4enr, (unsigned long) m.bdcr,
               (unsigned long) m.icsr, (unsigned long) m.prer,
               (unsigned long) m.ck_apre_hz);
        return;
    }

    printf("    RTC_PRER %08lX so the sub second tick is %lu Hz, "
           "gate %lu ticks\n",
           (unsigned long) m.prer, (unsigned long) m.ck_apre_hz,
           (unsigned long) m.ticks);
    printf("    MEASURED  %lu Hz over %lu counted core cycles\n",
           (unsigned long) m.core_hz_measured, (unsigned long) m.cycles);
    /* The derived figure with no parts per million of its own: the block below
     * prints the comparison, all three of it, from one implementation. */
    printf("    derived   %lu Hz, from the registers\n",
           (unsigned long) m.core_hz_derived);

    /* The nominal is printed separately from the derived figure because they are
     * different claims. The derived figure is what this image computed from the
     * registers; the nominal is what the datasheet says the oscillator is. For
     * the internal oscillator those differ by a known amount already measured by
     * two other instruments, and that is the whole point of this comparison.
     *
     * THREE NUMBERS AND NOT ONE, since Sunday 4 October 2026. This block used to
     * compute a single figure here and call it "parts per million against
     * nominal", which is the same ambiguity that put "about 0.117 per cent high"
     * into three files the same afternoon. A frequency that reads high, a
     * duration that comes out short and a delay that runs long are three
     * statements with two values and two signs between them, and the console is
     * where a reader meets them first. clocktree_bias computes them, the host
     * suite checks every one against a vector, and this prints what it returns
     * rather than repeating the arithmetic. */
    if (nominal_hz != 0u && m.core_hz_measured != 0u) {
        clocktree_bias_t bias;

        if (clocktree_bias(nominal_hz, m.core_hz_measured, &bias)) {
            printf("    nominal   %lu Hz, and against the measurement:\n",
                   (unsigned long) nominal_hz);
            printf("      the reported frequency is %ld ppm %s\n",
                   (long) (bias.frequency_ppm < 0 ? -bias.frequency_ppm
                                                  : bias.frequency_ppm),
                   bias.frequency_ppm < 0 ? "low" : "high");
            printf("      a measured duration comes out %ld ppm %s\n",
                   (long) (bias.duration_ppm < 0 ? -bias.duration_ppm
                                                 : bias.duration_ppm),
                   bias.duration_ppm < 0 ? "short" : "long");
            printf("      a requested delay is delivered %ld ppm %s\n",
                   (long) (bias.delay_ppm < 0 ? -bias.delay_ppm : bias.delay_ppm),
                   bias.delay_ppm < 0 ? "short" : "long");
        }
    }
}

/* ONE MEGAHERTZ, and the choice is not arbitrary.
 *
 * It divides both timer clocks exactly. At the reset clock APB2 is 64 MHz, so
 * the divider is 64 and the fields are PSC 0, ARR 63. At the 280 MHz setting
 * APB2 is 140 MHz, so the divider is 140 and the fields are PSC 0, ARR 139.
 * DIFFERENT FIELDS, SAME OUTPUT, which is what makes running the source twice
 * worth more than running it once: if the counter reads one megahertz at both
 * clocks, the APB2 decode is confirmed as well as the counting path, because a
 * wrong APB2 figure would produce a different frequency from the same request.
 *
 * It is also well under the counter's usable maximum of 16777216 Hz, and it
 * puts 3906 edges in each 256 Hz crystal tick, comfortably inside the 16-bit
 * counter between polls. A faster source would be a better test of the ceiling
 * and a worse test of the arithmetic, because a count near the wrap is a count
 * whose correctness depends on the wrap handling rather than on the path.
 *
 * AND ONE CONSTRAINT THAT IS EASY TO MISS. Every millihertz figure in this file
 * is printed through a cast to unsigned long, which is 32 bits on this target,
 * so a frequency above about 4.29 MHz truncates in the REPORT while being
 * perfectly correct in the arithmetic. One megahertz is 1000000000 millihertz,
 * inside that with room. This is a limit on what can be printed and not on what
 * can be measured, which is exactly the kind of difference that would otherwise
 * be read as a measurement result; freqcount's own line has carried the same
 * cast since Monday 5 October 2026 and the same limit with it. */
#define PWMSRC_TARGET_HZ  1000000u


/* A SECOND GATE, SHORT ENOUGH THAT THE COUNTER CANNOT WRAP TWICE. This exists
 * to falsify one model and nothing else, so it is worth stating what the model
 * is before the number arrives.
 *
 * ON TUESDAY 6 OCTOBER 2026 THE COUNTER COUNTED FOR THE FIRST TIME, with a wire
 * from CN10 pin 10 to CN10 pin 21, and reported 87903 and 82019 millihertz
 * against a source producing about a megahertz. Low by a factor of eleven to
 * twelve, and different between the two clocks.
 *
 * THE MODEL FOR THAT, read out of the code rather than guessed:
 * freqcount_measure_mhz samples the counter twice, once before the gate and once
 * after, and each sample increments the wrap count by at most one, because
 * LPTIM1_ISR's ARRM is a flag meaning one or more matches since ARRMCF and not a
 * count of them. So a gate spanning fifteen matches contributes one. The
 * reported edges are then 65536 plus the 16-bit residue, whatever the input,
 * which caps any one-second reading at 131071 Hz.
 *
 * Both readings sit on that model within 0.24 and 0.19 per cent, and both are
 * under the cap. That is agreement, not proof.
 *
 * SO HERE IS THE FALSIFIER. Twelve ticks of the 256 Hz crystal tick is 46.875
 * ms. Any source below 65536 / 0.046875 = 1398101 Hz puts fewer than two matches
 * inside that window, and the two-sample scheme is EXACT when at most one match
 * occurs: the before sample clears ARRM, the after sample counts the one that
 * happened, and the arithmetic closes. This source is about a megahertz.
 *
 *   if the short gate reads about a megahertz, the model is confirmed and the
 *   defect is the wrap accounting over long gates, nothing else
 *
 *   if the short gate ALSO reads 65536 plus a residue, the model is wrong, the
 *   fault is elsewhere, and a fix written against the model would be the wrong
 *   edit
 *
 * NO FIX IS IN THIS CHANGE, deliberately. The long gate is left exactly as it
 * was so that one capture shows both, same wire, same source, same run: the
 * broken reading is the control for the short one.
 *
 * 47 is the smallest whole number of milliseconds that asks for 12 ticks, since
 * freqmath_ticks_for_ms truncates 47 * 256 / 1000 to 12.
 *
 * AND THE RESOLUTION THE REPORT PRINTS IS COMPUTED FROM THE 47 AND NOT FROM THE
 * 46.875, because freqmath_resolution_mhz takes the gate a caller ASKED for. So
 * it says 21276 millihertz where the gate actually delivers 21333, which is 0.3
 * per cent out. That is small enough to leave alone and large enough to be worth
 * writing down rather than discovering later. */
#define FREQCOUNT_SHORT_GATE_MS  47u

/* The source frequency above which 12 ticks could hold two matches, so the
 * short gate would prove nothing. 65536 edges in 12/256 of a second. */
#define FREQCOUNT_SHORT_GATE_MAX_HZ  1398101u


/* The signal source, which is the half of the experiment this board can drive.
 *
 * Returns what it will produce, in millihertz, or 0 when it refused. The caller
 * passes that to report_freqcount so the two numbers can be compared in the one
 * place where both are known. */
static uint64_t report_pwmsrc(const char *when)
{
    pwmsrc_t src;
    const int rc = pwmsrc_start(PWMSRC_TARGET_HZ, &src);

    printf("\n  the signal source, TIM1 channel 3 on PE13, %s:\n", when);
    printf("    pwmsrc_start %s\n", pwmsrc_refusal_text(rc));
    if (rc != PWMSRC_OK) {
        return 0u;
    }

    printf("    timer clock      %lu Hz, APB2 with CDPPRE2 at divide by one\n",
           (unsigned long) src.tim_hz);
    printf("    asked for        %lu Hz\n", (unsigned long) src.want_hz);
    printf("    PSC %lu  ARR %lu  CCR3 %lu\n",
           (unsigned long) src.psc, (unsigned long) src.arr,
           (unsigned long) src.ccr);
    printf("    produces         %lu millihertz, %ld ppm from the target\n",
           (unsigned long) src.achieved_mhz, (long) src.error_ppm);
    return src.achieved_mhz;
}


/* The frequency counter, asked only whether it comes up.
 *
 * WHAT THIS PROVES AND WHAT IT CANNOT, stated first because the distinction is
 * the whole value of the section. Nothing is connected to PD12, and the pin is
 * configured with a pull-up, so it sits high and no edges arrive. The count is
 * therefore 0 and the reported frequency is 0, which is also what
 * freqcount_measure_mhz returns when it refuses. So this run says NOTHING about
 * whether the counting path works.
 *
 * What it does say is worth the lines anyway. freqcount_init returning 0 means
 * LPTIM1 accepted a configuration and read back an autoreload of 0xFFFF from the
 * address this repository DERIVED rather than read: ST's header gives LPTIM1 as
 * CD_APB1PERIPH_BASE + 0x2400 and this header knew that base only through
 * USART3. A wrong base would fail that read-back here rather than in P06 later.
 * And the ceiling is computed from the clock tree, so it should read 64 MHz
 * before the raise and 140 MHz after it, which is a second check on the same
 * decode the rest of this image is about.
 *
 * SINCE TUESDAY 6 OCTOBER 2026 THERE IS A SOURCE, so the paragraph above is
 * about the case where the wire is absent rather than about every case. The
 * source is TIM1 channel 3 on PE13 and the counter reads PD12; one wire from
 * Arduino D3 to Arduino D29 joins them.
 *
 * AN EARLIER VERSION OF THIS COMMENT PROPOSED SOMETHING ELSE AND WAS WRONG. It
 * named ST's LPTIM_PWMExternalClock example, which puts LPTIM1_OUT on PD13 and
 * LPTIM1_IN1 on PD12, adjacent pins, and called one wire between them a self
 * test. It is a feedback loop: that example clocks the counter from IN1 and
 * generates its output from the compare, so the output is derived from the
 * input and counting it establishes nothing. TIM1 shares nothing with the
 * counter, which is why it replaced it. */
static void report_freqcount(const char *when, uint64_t source_mhz)
{
    const int rc = freqcount_init();

    printf("\n  the frequency counter, %s:\n", when);
    printf("    freqcount_init %s\n",
           (rc == 0) ? "returned 0, so LPTIM1 took the configuration and its "
                       "autoreload read back"
                     : "REFUSED, which is a wrong address, a write that was "
                       "never acknowledged, or a crystal that did not start");
    if (rc != 0) {
        return;
    }

    const uint32_t ceiling = freqcount_ceiling_hz();
    printf("    sampling ceiling %lu Hz, the LPTIM1 kernel clock, which is APB1\n",
           (unsigned long) ceiling);
    printf("    usable maximum   16777216 Hz, one 16 bit wrap per crystal tick\n");
    printf("    resolution over a 1000 ms gate: %lu millihertz\n",
           (unsigned long) freqcount_resolution_mhz(1000u));

    const uint64_t mhz = freqcount_measure_mhz(1000u);

    if (source_mhz == 0u) {
        printf("    measured %lu millihertz with NO SOURCE RUNNING, which is\n"
               "    the expected answer and proves only that the path returns\n",
               (unsigned long) mhz);
        return;
    }

    if (mhz == 0u) {
        printf("    measured 0 millihertz while the source produces %lu.\n"
               "    THAT MEANS NO WIRE between PE13, which is Arduino D3, and\n"
               "    PD12, which is Arduino D29. The source is running and\n"
               "    nothing carries it to the counter, so this is the reading\n"
               "    to expect until the two pins are joined.\n",
               (unsigned long) source_mhz);
        return;
    }

    /* Both ends are alive, so this is the comparison the whole arrangement
     * exists for. The divide by 1000 turns the source's millihertz back into
     * hertz for pwmmath_error_ppm, which is exact at this target because one
     * megahertz is a whole number of millihertz. */
    const int32_t apart =
        pwmmath_error_ppm(mhz, (uint32_t) (source_mhz / 1000u));

    printf("    measured %lu millihertz against a source producing %lu,\n"
           "    which is %ld parts per million apart.\n",
           (unsigned long) mhz, (unsigned long) source_mhz, (long) apart);
    printf("    THE COUNTING PATH IS TESTED rather than merely configured:\n"
           "    these two numbers reach the same quantity through paths that\n"
           "    share no component but the crystal. If they disagree, the SIZE\n"
           "    says which kind of error it is: a shortfall that is a whole\n"
           "    number of 65536s is the wrap accounting, a ratio near a small\n"
           "    integer is a prescaler or divider misread, a few hundred ppm is\n"
           "    the crystal, and a figure that moves between runs is the\n"
           "    debugger's 8 MHz, which is known to move by a part in a\n"
           "    thousand.\n");

    /* AND THE SAME SIGNAL OVER A GATE TOO SHORT TO WRAP TWICE, which is the
     * falsifier for the wrap model and not a second opinion on the frequency.
     * The constant's comment carries the argument. */
    const uint32_t source_hz = (uint32_t) (source_mhz / 1000u);
    if (source_hz > FREQCOUNT_SHORT_GATE_MAX_HZ) {
        printf("\n    the short gate is SKIPPED: this source is %lu Hz, above the\n"
               "    %lu Hz at which 12 ticks could hold two counter matches, so\n"
               "    it would prove nothing\n",
               (unsigned long) source_hz,
               (unsigned long) FREQCOUNT_SHORT_GATE_MAX_HZ);
        return;
    }

    const uint64_t short_mhz = freqcount_measure_mhz(FREQCOUNT_SHORT_GATE_MS);
    const int32_t short_ppm = pwmmath_error_ppm(short_mhz, source_hz);

    printf("\n    the same signal over a SHORT gate, the falsifier:\n");
    printf("      asked %lu ms, which truncates to 12 ticks of 256 Hz, so\n"
           "      46.875 ms. Under one counter match for any source below\n"
           "      %lu Hz, and this one is %lu Hz\n",
           (unsigned long) FREQCOUNT_SHORT_GATE_MS,
           (unsigned long) FREQCOUNT_SHORT_GATE_MAX_HZ,
           (unsigned long) source_hz);
    printf("      measured %lu millihertz, %ld ppm from the source\n",
           (unsigned long) short_mhz, (long) short_ppm);
    printf("      resolution over the gate: %lu millihertz, about 21 Hz\n",
           (unsigned long) freqcount_resolution_mhz(FREQCOUNT_SHORT_GATE_MS));
    printf("      WHAT TO READ: near %lu confirms the wrap model and leaves the\n"
           "      defect in the long gate alone. Near 65536 plus a residue, so\n"
           "      roughly 65000 to 131000 Hz, refutes it and the fault is\n"
           "      somewhere else.\n",
           (unsigned long) source_hz);
    printf("      AND ONE MORE THING THE ppm ABOVE SHOULD SATISFY: the source is\n"
           "      APB2 divided by a whole number, so it follows this part's\n"
           "      clock, while the gate is the crystal. So that ppm figure\n"
           "      should come out equal to the clock's own offset printed above,\n"
           "      same sign and size. Two instruments, one quantity, and no\n"
           "      arithmetic added to make it so.\n");
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
    dump_registers("the registers as found");

    /* FIRST, on the reset clock, which is the measurement that discriminates.
     * The internal oscillator has already been measured at 64.17 to 64.18 MHz
     * by two other instruments, so this reading either agrees with them and
     * clears the crystal, or it does not and the crystal is the thing that is
     * wrong. Done before the clock change so a failure in the sequence below
     * still leaves this result on the console. */
    report_crystal("on the reset clock", HSI_HZ_NOMINAL);
    {
        const uint64_t src = report_pwmsrc("on the reset clock");
        report_freqcount("on the reset clock", src);
    }

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
               "with\n  the target. THAT IS A STATEMENT ABOUT THE REGISTERS AND NOT "
               "ABOUT\n  THE FREQUENCY. 280 MHz on the line above is 8 MHz of board "
               "fact\n  multiplied and divided by fields read back out of these "
               "registers,\n  so it tests the configuration and the arithmetic, not "
               "the\n  assumption underneath both: that the 8 MHz is 8 MHz.\n");
        printf("\n  THE MEASUREMENT IS BELOW, against the 32.768 kHz crystal, "
               "which is\n  the one reference on this board that does not come from "
               "the PLL\n  chain. Until Sunday 4 October 2026 this paragraph said "
               "the clock\n  was not measured and named freqcount.c as the "
               "instrument that\n  would settle it. Both halves were wrong: the "
               "crystal settles it,\n  and freqcount.c counts an EXTERNAL signal "
               "against the internal\n  clock, so it could never have witnessed the "
               "internal clock.\n");
        printf("\n  THAT YOU CAN READ THIS LINE IS THE BEST EXTERNAL CHECK HERE,\n"
               "  and it is stronger than it looks. The baud divider was just\n"
               "  recomputed from APB1 at 140 MHz, and the host's serial port is\n"
               "  clocked by the host. Text this clean bounds APB1 to about two\n"
               "  per cent of 140 MHz, and the core to about two per cent of 280,\n"
               "  since the two differ only by prescalers read back above. What it\n"
               "  cannot do is tell 280 MHz from 279, which is 0.36 per cent and\n"
               "  is what one wrong digit in N would give.\n");
        printf("\n  LD1 green on PB0 now blinks at one second per cycle by this\n"
               "  part's own count, which is NOT independent evidence: the delay\n"
               "  loop is calibrated against DWT_CYCCNT and that counts the clock\n"
               "  under test. Against a watch it is still worth a glance, because\n"
               "  it costs nothing and would catch an error of a factor.\n");
    }

    /* And again at the new clock. Same crystal, same gate, so the two readings
     * differ only in what they are measuring. */
    report_crystal("at the new clock", CORE_HZ_TARGET);
    {
        /* Re-planned rather than left running. TIM1 is on APB2, which just
         * changed from 64 to 140 MHz, so the fields that produced one megahertz
         * before would now produce 2.1875 MHz. Asking for the same frequency
         * again from a different timer clock is the point: different PSC and
         * ARR, the same output, counted against the same crystal. */
        const uint64_t src = report_pwmsrc("at the new clock");
        report_freqcount("at the new clock", src);
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
