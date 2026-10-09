/* marker.c: the one pin the witness watches.
 *
 * Toggled once per sample. Everything docs/measurement.md claims is measured
 * through this pin, so it has exactly one job and does it in as few
 * instructions as possible: a single store to the set/reset register, which is
 * atomic and needs no read, so it cannot be a race against anything else
 * touching the same port.
 *
 * ONE EDGE PER SAMPLE, ALTERNATING, AND NOT A PULSE. Until Friday 9 October
 * 2026 this file offered marker_pulse(), a set and a clear in two consecutive
 * stores, which holds the pin high for a few core cycles at 64 MHz: tens of
 * nanoseconds. The first capture through the MCC 118, which samples every
 * 10 microseconds, saw none of it and reported 102 edges in two seconds at
 * 50.67 Hz, which is mains hum on a pin held at 0 V, counted by thresholds
 * that adapt to whatever swing they are given. A toggle leaves the level
 * where the sample put it until the next sample, so the witness sees one
 * edge per sample, which is what MEASUREMENT.md and the handler comment in
 * acq_timer.c had described all along. The analysers count both polarities.
 *
 * WHICH PIN, AND IT IS SETTLED SINCE TUESDAY 6 OCTOBER 2026. The pin must be
 * free while no shield is fitted and must reach the Zio header so a wire can get
 * to it. PB4 satisfies both, from UM2408 Rev 6, which is MB1363's manual:
 *
 *   Table 18, page 44    CN7 pin 19, signal name D25, function SPI_B_MISO on
 *                        SPI3, Remark column empty
 *   page 37              "ST Zio connectors CN7, CN8, CN9, and CN10 are female
 *                        connectors on the top side", so CN7 is a Zio header
 *
 * ITS LABELLED FUNCTION IS A PERIPHERAL AND NOT A BOARD FUNCTION, which is what
 * "free" has to mean here. SPI_B_MISO is an alternate function the pin can take;
 * nothing on the board drives it. It is not LD1 on PB0, not LD2 on PE1, not LD3
 * on PB14, not the button on PC13, not the console on PD8 and PD9, not the
 * counting input on PD12 and not the signal source on PE13.
 *
 * AND THE TABLE WAS NEARLY THE WRONG ONE, which is worth more than the answer.
 * UM2408 documents several MB1363-family boards and tabulates each separately.
 * The first read landed on Table 17, pages 38 to 41, which is
 * "NUCLEO-H745ZI-Q and NUCLEO-H755ZI-Q pin assignments". Table 18, pages 42 to
 * 45, is this board's. The two tables carry the IDENTICAL row for PB4, so the
 * wrong table would have given the right answer, and the reasoning would still
 * have been wrong. That is the same shape as UM2407 against UM2408 and as
 * RCC_TypeDef's offset comments being the H743's: ST's material for the family
 * reads as material for the part until the caption is checked.
 *
 * ONE THING UM2408 DOES NOT SAY, so it is not claimed. Its footnote list gives
 * PB3 as D23/SWO, and says nothing about PB4. Whether PB4 carries a JTAG
 * function by default on this package is a datasheet question this has not read.
 * It does not affect the debugger in use: the ST-Link runs SWD, which needs
 * SWCLK on PA14, SWDIO on PA13 and SWO on PB3, and none of those is PB4.
 */
#include <stdint.h>

#include "marker.h"

#include "stm32h7xx.h"   /* CMSIS device header for this part, from the pack */

/* PB4, CN7 pin 19, Arduino D25. UM2408 Rev 6 Table 18 page 44, read Tuesday
 * 6 October 2026. The value is unchanged from the guess it replaces, which is
 * why the citation matters more than the number. */
#define MARKER_GPIO  GPIOB
#define MARKER_BIT   4u
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

/* One edge per call, alternating. The level is kept here and not read back
 * from ODR, so this is still a single store with no read of the port; the
 * only caller is the one handler, and nothing else touches PB4. marker_init
 * drives the pin low, and this static starts at zero, so the first call is
 * the first rising edge. */
void marker_toggle(void)
{
    static uint32_t is_high;
    is_high ^= 1u;
    MARKER_GPIO->BSRR = is_high ? (1u << MARKER_BIT)
                               : (1u << (MARKER_BIT + 16u));
}
