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
#include "acq_errors.h"
#include "marker.h"

#include "adcmath.h"
#include "cyccnt.h"
#include "pwmmath.h"

#include "stm32h7xx.h"

#include <stdio.h>
#include "adc_bringup.h"

/* Reached the way main.c reaches board_init, by declaration rather than by
 * include, because CMake puts only this project's c/ and c/instr on this
 * target's include path and the board support lives in neither. */
extern uint32_t board_pclk1_hz(void);

/* WHAT USED TO SIT HERE AND WHERE IT WENT, because this file was 889 lines and
 * is now a third of that, and a reader looking for the converter should not have
 * to search for it.
 *
 * The REFUSAL CODES moved to acq_errors.h on Friday 9 October 2026: they lived
 * here, so the other two back ends behind the same interface improvised their
 * own and the numbering collided, -3 meaning this file's APB1 prescaler and the
 * dma back end's unconfigured transfer engine at the same time.
 *
 * The CONVERTER BRING-UP moved to adc_bringup.c the same day, all eight steps
 * with the constants they are sourced from and the microsecond delay under them.
 * It is shared because the other two back ends need the same converter and only
 * the TRIGGER differs, which is now a parameter. report() went with it and is
 * called adc_report(), because the steps this file performs after the bring-up
 * returns must print in the same shape as the eight before them. And
 * apb1_divisor() followed it on Friday 9 October 2026, because the tick back end
 * needs the same HCLK and a third private copy was the alternative.
 *
 * WHAT STAYS IS WHAT IS ACTUALLY SPECIFIC TO THIS MECHANISM: the ring buffer,
 * the end-of-conversion handler, TIM6 itself, and the order in which the
 * converter is armed before the timer starts.
 */

#define BLOCK 64u

static uint16_t bank[2][BLOCK];
static volatile uint32_t widx;
static volatile uint8_t  wbank;
static volatile uint32_t ready_seq;
static volatile uint32_t taken_seq;
static volatile uint32_t overruns;
static volatile uint32_t conv_overruns;

void ADC_IRQHandler(void)
{
    const uint32_t isr = ADC1->ISR;

    /* THE CONVERTER'S OWN OVERRUN IS HANDLED FIRST, AND BEFORE THE EOC TEST
     * RATHER THAN AFTER IT, for a reason that is the whole point of watching it.
     *
     * OVR means a conversion finished while the previous result was still
     * sitting unread in DR. With OVRMOD clear, which is the value acq_start
     * reports as found rather than sets, the data register is PRESERVED and the
     * new result is discarded, and OVR stays set. An overrun therefore does not
     * merely lose one sample: until OVR is cleared the converter keeps
     * discarding, so an early return on a clear EOC would leave the flag set and
     * the stream would stop for good while every register still read back
     * correctly. That is the same class of silent failure as the missing vector,
     * and it is the reason this is not simply counted at the end.
     *
     * Clearing is a write of one, and the count is what main reports. NO MARKER
     * TOGGLE HERE: the marker must carry one edge per CONVERSION and not one per
     * interrupt, or the witness would measure this handler's entries rather than
     * the sampling instants, which is the one thing it exists to measure. */
    if ((isr & ADC_ISR_OVR) != 0u) {
        conv_overruns++;
        ADC1->ISR = ADC_ISR_OVR;
    }

    if ((isr & ADC_ISR_EOC) == 0u) {
        return;
    }

    marker_toggle();                   /* see the weakness noted above */
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
    /* THE TRIGGER IS THIS BUILD'S ONE CONTRIBUTION TO THE BRING-UP, passed rather
     * than compiled in so that the tick build can pass a software trigger and the
     * transfer-engine build can pass this same one. The words are passed too,
     * because a reader holding only the field value 13 cannot tell TIM6 TRGO from
     * any other source, and the step report prints them. */
    const int adc_rc = adc_bring_up(pclk1, apb1_divisor(),
                                    ADC_EXTSEL_TIM6_TRGO, ADC_EXTEN_RISING,
                                    "TIM6 TRGO on the rising edge");
    if (adc_rc != 0) {
        return adc_rc;
    }

    /* TWO THINGS THAT WERE MISSING FROM THIS FILE SINCE IT WAS WRITTEN, and that
     * only became reachable once the bring-up above stopped refusing. Both are
     * the quiet kind: the configuration would have read back perfectly and no
     * sample would ever have arrived.
     *
     * THE PERIPHERAL'S OWN INTERRUPT ENABLE WAS NEVER SET. NVIC_EnableIRQ below
     * tells the interrupt controller to accept the line; it does not tell the
     * converter to raise it. Without EOCIE in ADC1->IER the end of conversion
     * sets the flag in ISR and nothing else happens, so ADC_IRQHandler never
     * runs, acq_take never has a block, and main reports zero blocks forever
     * with every register correct.
     *
     * AND ADSTART WAS NEVER SET. With EXTEN configured the converter waits for
     * the trigger, but it only waits once it has been started: ADSTART is what
     * arms it. A TIM6 trigger arriving at an unarmed converter does nothing.
     *
     * The stale flags are cleared first, because ISR is not reset by
     * configuration and a left-over EOC would make the first interrupt report a
     * sample that predates this call. Both flags are cleared by writing one. */
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_OVR;
    ADC1->IER |= ADC_IER_EOCIE;
    const bool eocie = (ADC1->IER & ADC_IER_EOCIE) != 0u;
    adc_report("end of conversion irq", ADC_IER_EOCIE, ADC1->IER, eocie);
    if (!eocie) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    /* AND NOW OVRIE, WHICH THIS FILE NAMED AS A GAP FOR A DAY BEFORE CLOSING IT.
     *
     * The comment that stood here said OVRIE was deliberately not enabled, that
     * the converter's OVR and acq_overruns() are different things, that this
     * build reported the second and not the first, and that a run with converter
     * overruns would therefore look clean. All of that was true and it was a
     * description of a hole rather than a reason for one. The sampling run on
     * Wednesday 7 October 2026 reported overruns 0 for 390 blocks and that number
     * could not have been anything else, because nothing was watching the flag
     * that the application's own counter does not cover.
     *
     * Enabling the line is only half of it and the handler does the other half:
     * it counts OVR and clears it, so the count is a real count and the stream
     * recovers. Enabling an interrupt whose flag nobody clears would hang the
     * part in the handler, which is why these two changes belong in one commit.
     *
     * acq_conv_overruns() now returns 0 with the count for this back end, where
     * the other two still return -1 and say so.
     *
     * BOTH NAMES WERE VERIFIED BEFORE A LINE OF THIS WAS WRITTEN, in
     * stm32h7a3xxq.h of STM32Cube_FW_H7_V1.13.0, which is the Q-suffix header for
     * this exact part and not the family one: ADC_IER_OVRIE at line 2675 and
     * ADC_CFGR_OVRMOD at line 2784. The search carried ADC_IER_EOCIE at 2669 and
     * ADC_ISR_OVR at 2637 as controls, since both already compile in this file,
     * AND A DELIBERATELY ABSENT NAME that returned nothing, which is what proves
     * the check could have failed. Verifying names without a compiler is the
     * discipline this file adopted after a missing include cost a round trip. */
    ADC1->IER |= ADC_IER_OVRIE;
    const bool ovrie = (ADC1->IER & ADC_IER_OVRIE) != 0u;
    adc_report("overrun irq", ADC_IER_OVRIE, ADC1->IER, ovrie);
    if (!ovrie) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    /* OVRMOD IS REPORTED AS FOUND AND NOT SET, which is the as-found discipline
     * this volume adopted after the deep power-down step could not say whether a
     * bit had been cleared or had never been set.
     *
     * The bit decides what an overrun DOES to the data: clear means the data
     * register is preserved and the new conversion is discarded, set means the
     * new conversion overwrites it. A reader who knows the count but not this bit
     * cannot say which samples a gap contains. Nothing here chooses between them,
     * because neither is obviously right for this application and choosing would
     * be a configuration decision presented as a fact. The value is printed so
     * the choice can be made later with the number in hand. */
    printf("  adc overrun mode as found  %lu  (0 keeps the old sample, 1 the new)\r\n",
           (unsigned long) ((ADC1->CFGR & ADC_CFGR_OVRMOD) != 0u ? 1u : 0u));

    NVIC_SetPriority(ADC_IRQn, 5u);
    NVIC_EnableIRQ(ADC_IRQn);

    /* Arm the converter BEFORE the trigger source starts, so the first update
     * event is not emitted into an unarmed converter and lost. ADSTART is
     * read-set, so it is written with the same mask discipline as the rest. */
    ADC1->CR = (ADC1->CR & ~ADC_CR_READ_SET_BITS) | ADC_CR_ADSTART;
    const bool armed = (ADC1->CR & ADC_CR_ADSTART) != 0u;
    adc_report("armed for the trigger", ADC_CR_ADSTART, ADC1->CR, armed);
    if (!armed) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    TIM6->CR1 |= TIM_CR1_CEN;
    printf("  tim6 started, so conversions begin on its update event\r\n");
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

/* This back end watches the flag, so it answers with a count. The count is only
 * meaningful because the handler clears OVR; a build that enabled OVRIE without
 * clearing would report one overrun and then never return from the handler. */
int acq_conv_overruns(uint32_t *out)
{
    *out = conv_overruns;
    return 0;
}

const char *acq_name(void) { return "timer"; }

