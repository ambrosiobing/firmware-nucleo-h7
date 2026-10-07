/* acq_timer.c: build 2, the timer starts the conversion in hardware.
 *
 * TIM6 counts up and emits a trigger output on its update event. The converter
 * is configured to start on that trigger. The processor is not in the path of
 * the timing at all: it learns a sample exists afterwards, from the
 * end-of-conversion interrupt, and by then the instant has already been fixed
 * by hardware.
 *
 * The prediction written in docs/measurement.md is that this build and build 3
 * are indistinguishable on the jitter criterion, because in both the sample
 * instant is produced by hardware, and that both beat build 1. Writing that
 * down in advance is what makes finding otherwise a result.
 *
 * ONE HONEST WEAKNESS. The marker is still toggled in software, in the
 * end-of-conversion handler, so the marker edge carries the interrupt latency
 * even though the sample instant does not. The witness therefore measures the
 * regularity of the handler, not of the conversion. The proper fix is to let a
 * timer channel drive the pin directly so no software is involved, which is
 * the stretch goal at the end of the chapter and is the only way this method
 * can separate the two.
 */
#include "acq.h"
#include "marker.h"

#include "adcmath.h"
#include "cyccnt.h"
#include "pwmmath.h"

#include "stm32h7xx.h"

#include <stdio.h>

/* Reached the way main.c reaches board_init, by declaration rather than by
 * include, because CMake puts only this project's c/ and c/instr on this
 * target's include path and the board support lives in neither. */
extern uint32_t board_pclk1_hz(void);

/* Why acq_start refused, as distinct values rather than one. The clock280.c
 * lesson applies here exactly: "the converter did not come up" is not a finding
 * anybody can act on, and "the regulator wrote 10000000 and read back 0" is. */
#define ACQ_ERR_PCLK1_UNKNOWN     (-2)
#define ACQ_ERR_APB1_PRESCALER    (-3)
#define ACQ_ERR_NO_CYCLE_COUNTER  (-4)
#define ACQ_ERR_ADC_DEEPPWD       (-5)
#define ACQ_ERR_ADC_VREG          (-6)
#define ACQ_ERR_ADC_CLOCK_MODE    (-7)

/* THE SEVEN READ-SET BITS, from stm32h7xx_hal_adc.c line 366 of
 * STM32Cube_FW_H7_V1.13.0, where ST names them ADC_CR_BITS_PROPERTY_RS and
 * states the rule: "writing 0 has no effect on the bit value".
 *
 * That is why the read-modify-write habit every other register in this
 * repository uses is WRONG here, and wrong in the quiet direction: clearing one
 * of these by writing zero does nothing at all and reports nothing. ST does not
 * read-modify-write CR; it forces these to their reset state on every write,
 * and steps 1 and 2 below do the same. The mask is defined here rather than
 * taken from the HAL because this project compiles the device header only.
 *
 * ADC_CR_BITS_PROPERTY_RS is a HAL-private macro, so these seven names come
 * from the device header and the grouping comes from that line. */
#define ADC_CR_READ_SET_BITS  (ADC_CR_ADCAL     | ADC_CR_JADSTP   | \
                               ADC_CR_ADSTP     | ADC_CR_JADSTART | \
                               ADC_CR_ADSTART   | ADC_CR_ADDIS    | \
                               ADC_CR_ADEN)

/* tADCVREG_STUP, the internal regulator start-up time, from
 * stm32h7xx_ll_adc.h line 1537 where ST names it
 * LL_ADC_DELAY_INTERNAL_REGUL_STAB_US. ST's own comment calls for a DELAY and
 * not a poll: there is no ready flag for this one, so there is nothing to poll.
 * A converter enabled before this has elapsed is the usual reason a first
 * attempt reads zero for ever. */
#define ADC_VREG_STARTUP_US  10u

#define BLOCK 64u

static uint16_t bank[2][BLOCK];
static volatile uint32_t widx;
static volatile uint8_t  wbank;
static volatile uint32_t ready_seq;
static volatile uint32_t taken_seq;
static volatile uint32_t overruns;

void ADC_IRQHandler(void)
{
    if ((ADC1->ISR & ADC_ISR_EOC) == 0u) {
        return;
    }

    marker_pulse();                    /* see the weakness noted above */
    uint16_t v = (uint16_t) ADC1->DR;  /* reading DR clears the flag */

    uint32_t i = widx;
    bank[wbank][i] = v;
    i++;
    if (i >= BLOCK) {
        if (ready_seq != taken_seq) {
            overruns++;
        }
        ready_seq++;
        wbank ^= 1u;
        i = 0u;
    }
    widx = i;
}

/* The APB1 prescaler as a divisor, from the three encodings this volume has
 * sourced. Anything else returns 0, which pwmmath_timer_hz then refuses, and
 * that refusal is correct rather than unfortunate: reporting an undivided
 * frequency here would be wrong by exactly the ratio nobody would suspect.
 *
 * This is c/instr/pwmsrc.c's apb2_divisor with CDPPRE1 in place of CDPPRE2,
 * read from the sibling before writing this rather than derived again. The
 * field values are 0 for divide by one, 4 for two and 5 for four. */
static uint32_t apb1_divisor(void)
{
    const uint32_t field =
        (RCC->CDCFGR2 & RCC_CDCFGR2_CDPPRE1) >> RCC_CDCFGR2_CDPPRE1_Pos;
    switch (field) {
    case 0x0u: return 1u;
    case 0x4u: return 2u;
    case 0x5u: return 4u;
    default:   return 0u;
    }
}

/* A delay in microseconds, measured on the cycle counter rather than counted in
 * loop iterations.
 *
 * The cycle counter is the instrument this project already verified at boot:
 * cyccnt_init enables it and checks that it moved, and cyccnt_tick_hz refuses
 * with 0 when the rate could not be established. So a delay here is either
 * measured or declined, and never a loop whose duration nobody knows. Returns
 * false when there is no rate, which the caller turns into a refusal: a
 * regulator start-up wait of unknown length is not a wait. */
static bool delay_us(uint32_t us)
{
    const uint32_t hz = cyccnt_tick_hz();
    if (hz == 0u) {
        return false;
    }

    /* 64-bit so the multiply cannot wrap, and rounded up so the delay is never
     * shorter than asked. At 280 MHz, 10 microseconds is 2800 ticks, far inside
     * one wrap of the 32-bit counter. */
    const uint64_t want = (((uint64_t) hz * (uint64_t) us) + 999999u) / 1000000u;
    const uint32_t ticks = (uint32_t) want;
    const uint32_t t0 = cyccnt_now();

    while ((cyccnt_now() - t0) < ticks) {
        /* Unsigned subtraction is correct across one wrap. */
    }
    return true;
}

/* One line per step, in the shape clock280.c settled on: what was intended and
 * what the register actually holds, both printed, because that pair is what
 * turns a failed bring-up into a readable fault. A wrong peripheral base reads
 * back the reset value, a reserved field reads back zero, and a field that moved
 * between parts reads back something that is neither. */
static void report(const char *step, uint32_t wrote, uint32_t read_back, bool ok)
{
    printf("  adc %-22s %s  wrote %08lX  read back %08lX\r\n",
           step, ok ? "ok     " : "REFUSED",
           (unsigned long) wrote, (unsigned long) read_back);
}

/* The converter bring-up, steps 1 to 3 of the eight in the project README.
 *
 * WHAT THIS DOES AND WHERE IT STOPS, stated here because stopping is the result.
 * Steps 1, 2 and 3 need no frequency at all, so they are implementable today and
 * they carry the two traps worth having: the read-set bits, and a start-up time
 * with no ready flag to poll. Step 4 is boost mode, whose input is the ADC
 * kernel clock, and that clock depends on a choice between synchronous and
 * asynchronous mode that this project has not made and justified yet. ST does
 * not make it either: ADC_ConfigureBoostMode branches on it at
 * stm32h7xx_hal_adc.c line 3942, taking rcc_hclk1 divided by CKMODE in one case
 * and a dedicated kernel clock in the other.
 *
 * So this refuses at step 4 by name rather than picking a divisor. Calibration
 * and the enable erratum loop sit at step 5, AFTER boost in ST's order, and
 * reordering them to get a result sooner would calibrate the analogue path for a
 * boost setting it is not going to run at. */
static int adc_bring_up(void)
{
    /* The peripheral's bus clock first, for the reason freqcount_init gives:
     * every register write below goes nowhere without it, and goes nowhere
     * silently. Read back, because the write is posted. */
    RCC->AHB1ENR |= RCC_AHB1ENR_ADC12EN;
    (void) RCC->AHB1ENR;

    /* Step 1, from stm32h7xx_ll_adc.h line 6823. Leave deep power-down AND
     * force the seven read-set bits to their reset state in the written value,
     * which is what ST does and is not the same as a read-modify-write. */
    const uint32_t cr1 = ADC1->CR & ~(ADC_CR_DEEPPWD | ADC_CR_READ_SET_BITS);
    ADC1->CR = cr1;
    const bool deeppwd_clear = (ADC1->CR & ADC_CR_DEEPPWD) == 0u;
    report("leave deep power-down", cr1, ADC1->CR, deeppwd_clear);
    if (!deeppwd_clear) {
        return ACQ_ERR_ADC_DEEPPWD;
    }

    /* Step 2, from line 6856. The regulator on, the same way. */
    const uint32_t cr2 = (ADC1->CR & ~ADC_CR_READ_SET_BITS) | ADC_CR_ADVREGEN;
    ADC1->CR = cr2;
    const bool vreg_set = (ADC1->CR & ADC_CR_ADVREGEN) != 0u;
    report("regulator enable", cr2, ADC1->CR, vreg_set);
    if (!vreg_set) {
        return ACQ_ERR_ADC_VREG;
    }

    /* Step 3, from line 1537. Ten microseconds, and there is no flag for it. */
    if (!delay_us(ADC_VREG_STARTUP_US)) {
        report("regulator start-up", ADC_VREG_STARTUP_US, 0u, false);
        return ACQ_ERR_NO_CYCLE_COUNTER;
    }
    report("regulator start-up", ADC_VREG_STARTUP_US, ADC1->CR, true);

    /* Step 4, and this is where it stops. See the note above this function. */
    printf("  adc %-22s REFUSED  the clock mode is unchosen\r\n", "boost mode");
    printf("      synchronous takes rcc_hclk1 over CKMODE, asynchronous takes\r\n");
    printf("      a dedicated kernel clock. adcmath_boost and adcmath_clock_hz\r\n");
    printf("      are written and host tested; the input is the open question.\r\n");
    return ACQ_ERR_ADC_CLOCK_MODE;
}

int acq_start(void)
{
    marker_init();

    widx = 0u;
    wbank = 0u;
    ready_seq = 0u;
    taken_seq = 0u;
    overruns = 0u;

    RCC->APB1LENR |= RCC_APB1LENR_TIM6EN;
    (void) RCC->APB1LENR;              /* the write is posted; read it back */

    /* TIM6 at exactly 1 kHz, and the timer clock is now derived rather than
     * refused.
     *
     * THIS BLOCK USED TO RETURN -2 UNCONDITIONALLY, with a comment saying the
     * APB1 timer clock was unconfirmed and that whether the timer clock equals
     * the bus clock or twice it was the factor between 1 kHz and 2 kHz. That is
     * exactly the question pwmmath_timer_hz answers, from ST's own
     * stm32h7xx_hal_rcc_ex.h lines 3596 to 3605, sourced Tuesday 6 October 2026:
     * the timer kernel clock is min(HCLK, 2 x PCLK) with TIMPRE clear and
     * min(HCLK, 4 x PCLK) with it set. Fourteen cases of that rule are checked
     * on the host against a second derivation, so the factor is settled and no
     * longer guessed.
     *
     * Three things can still refuse, and each says which. */
    const uint32_t pclk1 = board_pclk1_hz();
    if (pclk1 == 0u) {
        /* The clock tree was not decoded, so there is no honest frequency to
         * plan against. */
        return ACQ_ERR_PCLK1_UNKNOWN;
    }

    /* TIMPRE read rather than assumed. Nothing in this repository writes the
     * bit, so it is whatever reset left, which is clear; reading it costs one
     * load and means this does not depend on that staying true. */
    const bool timpre = (RCC->CFGR & RCC_CFGR_TIMPRE) != 0u;
    const uint32_t timer_clk_hz = pwmmath_timer_hz(pclk1, apb1_divisor(), timpre);
    if (timer_clk_hz == 0u) {
        /* Either CDPPRE1 holds a ratio this volume has not sourced, or the
         * rule does not cover the combination. Refusing is correct: a build
         * that guessed the clock would produce a clean looking square wave at
         * the wrong rate, and the whole point of chapter 6 is that such a wave
         * is indistinguishable from a right one without an external witness. */
        return ACQ_ERR_APB1_PRESCALER;
    }

    printf("  tim6 clock            %lu Hz  (pclk1 %lu, /%lu, timpre %u)\r\n",
           (unsigned long) timer_clk_hz, (unsigned long) pclk1,
           (unsigned long) apb1_divisor(), (unsigned) timpre);

    const uint32_t ticks = timer_clk_hz / ACQ_RATE_HZ;
    uint32_t psc = 0u;
    uint32_t arr = ticks;
    while (arr > 0xFFFFu) {            /* TIM6 is a 16-bit counter */
        psc++;
        arr = ticks / (psc + 1u);
    }

    TIM6->CR1 = 0u;
    TIM6->PSC = psc;
    TIM6->ARR = arr - 1u;
    TIM6->CNT = 0u;
    TIM6->EGR = TIM_EGR_UG;            /* load the prescaler immediately */

    /* Trigger output on the update event. This is the line that makes the
     * timing a hardware property. */
    TIM6->CR2 = (TIM6->CR2 & ~TIM_CR2_MMS) | (2u << TIM_CR2_MMS_Pos);

    /* The converter, which is what the trigger above exists to start. It
     * reports each step and refuses at the first input it does not have.
     *
     * This is attempted BEFORE the timer is enabled, so a refusal leaves TIM6
     * configured and stopped rather than emitting a trigger output that nothing
     * consumes. That is clock280.c's rule applied here: return without the
     * irreversible step, so the part is left in a state somebody can read. */
    const int adc_rc = adc_bring_up();
    if (adc_rc != 0) {
        return adc_rc;
    }

    /* STILL TO BE SOURCED, and these three are what stand between the lines
     * above and a sample. EXTSEL is 13 for TIM6 TRGO, from stm32h7xx_ll_adc.h
     * line 993, so that one value IS in hand; what is not is the rising-edge
     * value for the EXTEN field beside it, which channel this project samples
     * and its sampling time, and the resolution. The first is one more line of
     * the same LL header. The other three want the datasheet, which
     * docs/the-board-and-the-wiring.md records as unread. */

    NVIC_SetPriority(ADC_IRQn, 5u);
    NVIC_EnableIRQ(ADC_IRQn);

    TIM6->CR1 |= TIM_CR1_CEN;
    return 0;
}

int acq_take(acq_block_t *out)
{
    uint32_t r = ready_seq;
    if (r == taken_seq) {
        return 0;
    }
    out->samples = bank[(r - 1u) & 1u];
    out->count   = BLOCK;
    out->seq     = r;
    taken_seq    = r;
    return 1;
}

uint32_t acq_overruns(void) { return overruns; }

const char *acq_name(void) { return "timer"; }
