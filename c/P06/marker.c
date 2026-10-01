/* marker.c: the one pin the witness watches.
 *
 * Toggled once per sample. Everything docs/measurement.md claims is measured
 * through this pin, so it has exactly one job and does it in as few
 * instructions as possible: a single store to the set/reset register, which is
 * atomic and needs no read, so it cannot be a race against anything else
 * touching the same port.
 *
 * WHICH PIN. The pin must be free while no shield is fitted and must reach the
 * Zio header so a wire can get to it. That is item 13 on the confirm list of
 * the authoring guide and it is NOT settled here. The two candidates below are
 * the usual free ones on a Nucleo-144, and the board user manual MB1363 decides
 * between them. Until it is read, MARKER_PIN is a guess and is marked as one.
 */
#include "marker.h"

#include "stm32h7xx.h"   /* CMSIS device header for this part, from the pack */

/* TO BE CONFIRMED against MB1363: that this pin is on the Zio header, is not
 * used by the board itself, and does not collide with a shield. */
#define MARKER_GPIO  GPIOB
#define MARKER_BIT   4u                    /* PB4, candidate */
#define MARKER_RCC_ENABLE()  do { \
        RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN; \
        (void) RCC->AHB4ENR;               /* read back: the write is posted */ \
    } while (0)

void marker_init(void)
{
    MARKER_RCC_ENABLE();

    /* Output, push-pull, no pull, and the fastest slew the pin offers. Slew
     * matters here and only here: the witness interpolates the crossing, so a
     * slow edge widens the uncertainty on every edge time in the run. */
    const uint32_t pos2 = MARKER_BIT * 2u;
    MARKER_GPIO->MODER   = (MARKER_GPIO->MODER & ~(3u << pos2)) | (1u << pos2);
    MARKER_GPIO->OTYPER &= ~(1u << MARKER_BIT);
    MARKER_GPIO->PUPDR  &= ~(3u << pos2);
    MARKER_GPIO->OSPEEDR |= (3u << pos2);

    marker_low();
}

/* Deliberately not inline and deliberately trivial. The cost of the call is
 * part of what chapter 6 measures, so hiding it in a macro would remove the
 * thing under test from the thing being tested. */
void marker_high(void) { MARKER_GPIO->BSRR = (1u << MARKER_BIT); }
void marker_low(void)  { MARKER_GPIO->BSRR = (1u << (MARKER_BIT + 16u)); }

void marker_pulse(void)
{
    MARKER_GPIO->BSRR = (1u << MARKER_BIT);
    MARKER_GPIO->BSRR = (1u << (MARKER_BIT + 16u));
}
