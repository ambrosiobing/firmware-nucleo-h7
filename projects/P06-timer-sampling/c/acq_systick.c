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
#include "acq_errors.h"
#include "marker.h"
#include "board.h"

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

    /* The tick, and the one line here that depends on chapter 1's clock tree
     * being right. The rate comes from board_core_hz() rather than from the
     * vendor's SystemCoreClock, for the reason recorded at length in
     * c/instr/cyccnt.c: that symbol lives in a vendor file this repository
     * does not compile, and inventing a definition for it would publish a
     * core frequency nobody measured.
     *
     * Zero is checked before the division and not only because dividing by a
     * rate of zero would be wrong in the other direction. A reload of zero
     * reaches SysTick_Config, which rejects it, and the function would then
     * return -1 for what reads like a hardware fault. The sampling rate is the
     * whole subject of P06, so a refusal here has to say that the rate was
     * never established.
     *
     * AND THIS COMMENT SAW THE PROBLEM AND THEN COMMITTED IT. It worried that -1
     * reads like a hardware fault, and the function below then returned -1 for
     * BOTH causes, so the rate being unknown and the reload being rejected were
     * indistinguishable from the console. Closed Friday 9 October 2026: the two
     * now have their own codes in acq_errors.h, which also records why -1 was
     * never acq_start's to use. */
    uint32_t core_hz = board_core_hz();
    if (core_hz == 0u) {
        return ACQ_ERR_CORE_HZ_UNKNOWN;
    }
    if (SysTick_Config(core_hz / ACQ_RATE_HZ) != 0u) {
        return ACQ_ERR_SYSTICK_RELOAD;
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

/* NOT WATCHED HERE, so this returns -1 rather than 0, and the difference is the
 * point of the interface. This back end polls EOC and reads DR inside the tick,
 * so an overrun needs the tick to be late by a whole sampling period, which is
 * less likely than in the interrupt-driven build and is NOT impossible: a tick
 * delayed by a higher-priority handler would do it. Printing 0 would claim the
 * flag had been watched throughout. It has not been, because this back end has
 * never been brought up on the board at all, and instrumenting a path that
 * refuses before it reaches the converter would be guessing at what it will do.
 * The line to add when it is brought up is the same one acq_timer.c now has. */
int acq_conv_overruns(uint32_t *out)
{
    (void) out;
    return -1;
}

const char *acq_name(void) { return "systick"; }
