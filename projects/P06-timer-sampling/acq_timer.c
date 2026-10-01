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

#include "stm32h7xx.h"

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

    /* TIM6 at exactly 1 kHz.
     *
     * TO BE CONFIRMED against RM0455: the APB1 timer clock for this part, and
     * whether the timer clock equals the bus clock or twice it. That factor is
     * the difference between 1 kHz and 2 kHz and it is not guessed here. The
     * two constants below are written as one expression so there is a single
     * place to correct once the manual is read.
     */
    const uint32_t timer_clk_hz = 0u;  /* TO BE CONFIRMED, see above */
    if (timer_clk_hz == 0u) {
        /* Refusing to run is the correct behaviour. A build that guessed the
         * clock would produce a clean looking square wave at the wrong rate,
         * and the whole point of chapter 6 is that such a wave is
         * indistinguishable from a right one without an external witness. */
        return -2;
    }

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

    /* TO BE CONFIRMED: the external trigger selection value that names TIM6's
     * trigger output for this converter on this part, and the rising-edge
     * enable. This is a table lookup in RM0455 and not something to infer. */

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
