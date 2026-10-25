/* acq_timer.c: build 2, the timer starts the conversion in hardware.
 *
 * TIM3 counts up and emits a trigger output on its update event. The converter
 * is configured to start on that trigger. The processor is not in the path of
 * the timing at all: it learns a sample exists afterwards, from the
 * end-of-conversion interrupt, and by then the instant has already been fixed
 * by hardware.
 *
 * The prediction written in docs/measurement.md is that this build and build 3
 * are indistinguishable on the jitter criterion, because in both the sample
 * instant is produced by hardware, and that both improve on build 1. Writing that
 * down in advance is what makes finding otherwise a result.
 *
 * THE HONEST WEAKNESS THAT STOOD HERE IS CLOSED, and it is worth reading what it
 * said before reading what replaced it. It said the marker was still toggled in
 * software, in the end-of-conversion handler, so the marker edge carried the
 * interrupt latency even though the sample instant did not, and that the witness
 * therefore measured the regularity of the handler rather than of the
 * conversion. It stood from Wednesday 7 October 2026 to Sunday 25 October 2026
 * and every published figure of this build was taken under it.
 *
 * WHAT CLOSED IT. TIM3 channel 1 drives the marker pin in hardware, so the edge
 * and the conversion trigger come from one counter and no software sits between
 * them. The handler no longer touches the pin. Joseph took that decision on
 * Tuesday 20 October 2026 on the ground that a comparison the instrument cannot
 * perform should not be published; the build arrived on Sunday 25 October 2026,
 * once PB4's alternate function and the ADC's trigger list had both been read.
 *
 * WHAT IT COSTS, and it is not nothing. This build and the transfer-engine build
 * now mark from the same channel of the same timer, so their marker signals are
 * identical by construction and no witness can tell them apart. That is correct
 * rather than unfortunate: with a hardware trigger the sampling instants ARE the
 * timer's, so two builds that share a timer share their instants. The difference
 * between a per-sample interrupt and a double-buffer transfer shows up in the
 * counters instead, which is where it lives.
 */
#include "acq.h"
#include "acq_errors.h"

#include "adcmath.h"
#include "cyccnt.h"

#include "stm32h7xx.h"

#include <stdio.h>
#include "adc_bringup.h"
#include "tim3_bringup.h"

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
 * the end-of-conversion handler, TIM3 itself, and the order in which the
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
     * TOGGLE HERE, and since Sunday 25 October 2026 there is none anywhere in
     * this handler: the marker must carry one edge per CONVERSION and not one
     * per interrupt, and the only arrangement that guarantees it is a timer
     * channel driving the pin with no software in the path at all. */
    if ((isr & ADC_ISR_OVR) != 0u) {
        conv_overruns++;
        ADC1->ISR = ADC_ISR_OVR;
    }

    if ((isr & ADC_ISR_EOC) == 0u) {
        return;
    }

    /* THE MARKER TOGGLE THAT STOOD HERE IS GONE, Sunday 25 October 2026. It was
     * the first statement in this branch, deliberately, so that the edge carried
     * as little of the handler's own duration as possible. TIM3 channel 1 now
     * puts the edge on the pin from the same counter that triggered this
     * conversion, so nothing this handler does can move it. */
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
    /* NO marker_init() HERE ANY MORE, Sunday 25 October 2026. The pin is handed
     * to TIM3 channel 1 by tim3_bring_up() below, in the same function that
     * configures the channel, because a pad and the channel that drives it are
     * one mechanism and a caller who configures one and forgets the other gets
     * perfect read-backs and a flat line on the witness. */
    widx = 0u;
    wbank = 0u;
    ready_seq = 0u;
    taken_seq = 0u;
    overruns = 0u;

    /* THE TIMER LEFT THIS FILE on Monday 19 October 2026, for tim6_bringup.c,
     * so that the transfer-engine back end reaches the same timer instead of
     * carrying a second copy of it. Joseph took that decision over the two
     * alternatives, which were to duplicate the setup there with the
     * duplication named in a comment, as option A did for apb1_divisor(), or to
     * leave that back end refusing: a known duplication is not a reason to
     * leave code unwritten, and a named one still leaves two copies that drift.
     *
     * AND IT BECAME TIM3 ON Sunday 25 October 2026, when the marker moved into
     * hardware. TIM6 is a basic timer with no channel and no pin, RM0455 Rev 13
     * section 46.2 page 1754, so it can start a conversion and can never mark
     * one. TIM3 can do both, so one timer carries the cadence and the evidence
     * of the cadence instead of two carrying one each. tim6_bringup.c went with
     * the change rather than staying linked and uncalled; see tim3_bringup.h for
     * what came across with it and what the predictions are.
     *
     * Every refusal this used to make, it still makes, with the same numbers
     * from acq_errors.h, because the caller returns them unchanged, and there is
     * one more: ACQ_ERR_MARKER_CHANNEL when the channel's registers do not take.
     *
     * The timer is left CONFIGURED AND STOPPED. It is started at the end of this
     * function, after the converter is armed, and that ordering is the reason the
     * two halves are separate functions rather than one. */
    const int tim_rc = tim3_bring_up(ACQ_RATE_HZ);
    if (tim_rc != 0) {
        return tim_rc;
    }

    /* The bus clock is still needed HERE, for the converter rather than the
     * timer: its boost setting and its kernel clock are both derived from it.
     * tim3_bring_up() has already refused if this were zero, so it is read
     * without a second check and the check is not duplicated. */
    const uint32_t pclk1 = board_pclk1_hz();

    /* The converter, which is what the trigger above exists to start. It
     * reports each step and refuses at the first input it does not have.
     *
     * This is attempted BEFORE the timer is enabled, so a refusal leaves TIM3
     * configured and stopped rather than emitting a trigger output that nothing
     * consumes. That is clock280.c's rule applied here: return without the
     * irreversible step, so the part is left in a state somebody can read.
     *
     * A refusal here now also leaves the marker pin on a stopped channel, which
     * is the right state to leave it in: low, driven, and not toggling. A
     * witness recording through that refusal sees a flat line, which is what a
     * build that never started should look like. */
    /* THE TRIGGER IS THIS BUILD'S ONE CONTRIBUTION TO THE BRING-UP, passed rather
     * than compiled in so that the tick build can pass a software trigger and the
     * transfer-engine build can pass this same one. The words are passed too,
     * because a reader holding only the field value 4 cannot tell TIM3 TRGO from
     * any other source, and the step report prints them. */
    const int adc_rc = adc_bring_up(pclk1, apb1_divisor(),
                                    ADC_EXTSEL_TIM3_TRGO, ADC_EXTEN_RISING,
                                    "TIM3 TRGO on the rising edge");
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
     * arms it. A TIM3 trigger arriving at an unarmed converter does nothing.
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

    tim3_start();
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

