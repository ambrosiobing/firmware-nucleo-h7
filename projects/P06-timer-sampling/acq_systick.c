/* acq_systick.c: build 1, the simplest thing that can work.
 *
 * The tick exception fires at 1 kHz, toggles the marker, starts a conversion,
 * waits for it, stores the result, returns. It is a real variant and not a
 * strawman: plenty of shipped firmware samples exactly this way, and it is the
 * baseline the other two have to beat on the jitter criterion.
 *
 * Its weakness is structural rather than accidental, and naming it in advance
 * is what makes the measurement a test rather than a demonstration. The edge
 * is produced by software, so its timing carries every delay between the
 * hardware event and the first instruction of the handler: exception entry,
 * any higher-priority interrupt that happens to be in progress, and on this
 * core whether the handler and its data are in a cached region. Chapter 19 is
 * about that last one.
 */
#include "acq.h"
#include "marker.h"

#include "stm32h7xx.h"

#define BLOCK 64u          /* samples per block handed to the application */

static uint16_t bank[2][BLOCK];
static volatile uint32_t widx;        /* write index within the current bank */
static volatile uint8_t  wbank;       /* bank the handler is filling */
static volatile uint32_t ready_seq;   /* incremented when a bank fills */
static volatile uint32_t taken_seq;   /* incremented when one is read */
static volatile uint32_t overruns;

/* Blocking single conversion. Its cost is part of what this build is measured
 * on, so it is not hidden.
 *
 * TO BE CONFIRMED against RM0455 and the datasheet, and NONE of it is settled
 * here: which converter and which channel reach an accessible pin on this
 * board, the kernel clock source and its prescaler, the boot and calibration
 * sequence this part wants, and the resolution bits. On this family the
 * converter needs an explicit exit from its own power-down state before any
 * other register write is accepted, and skipping that is the usual reason a
 * first attempt reads zero for ever.
 */
static uint16_t adc_convert_blocking(void)
{
    ADC1->CR |= ADC_CR_ADSTART;
    while ((ADC1->ISR & ADC_ISR_EOC) == 0u) {
        /* Busy wait, on purpose. This is the build where the processor is in
         * the path of the timing, and that is the thing being measured. */
    }
    return (uint16_t) ADC1->DR;
}

void SysTick_Handler(void)
{
    /* The marker first, before anything that could vary in duration. Whatever
     * follows it, the edge has already happened, so the edge time carries only
     * the delay ahead of it and not the work behind it. */
    marker_pulse();

    uint16_t v = adc_convert_blocking();

    uint32_t i = widx;
    bank[wbank][i] = v;
    i++;
    if (i >= BLOCK) {
        /* Hand this bank over and start filling the other one. If the
         * application has not taken the previous block, the samples are still
         * correct but the record has a gap, and a run with a gap is not
         * evidence, so it is counted. */
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

    /* TO BE CONFIRMED: the converter bring-up sequence for this part, as
     * above. Deliberately absent rather than guessed, because a plausible
     * looking sequence copied from the H743 is exactly the failure this whole
     * volume warns about. */

    /* The tick. SystemCoreClock is set by the clock tree of chapter 1, so this
     * is the one line here that depends on that chapter being right. If the
     * reload value does not fit 24 bits the tick cannot be produced at all and
     * that is an error rather than a silently wrong rate. */
    if (SysTick_Config(SystemCoreClock / ACQ_RATE_HZ) != 0u) {
        return -1;
    }
    return 0;
}

int acq_take(acq_block_t *out)
{
    uint32_t r = ready_seq;
    if (r == taken_seq) {
        return 0;
    }
    /* The handler is filling the other bank, so this one is not moving. */
    out->samples = bank[(r - 1u) & 1u];
    out->count   = BLOCK;
    out->seq     = r;
    taken_seq    = r;
    return 1;
}

uint32_t acq_overruns(void) { return overruns; }

const char *acq_name(void) { return "systick"; }
