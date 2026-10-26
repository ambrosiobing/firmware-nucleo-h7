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
 * 10 microseconds, caught fragments of it in 346 samples out of 199 784, none
 * above 0.49 V, and reported 102 edges in two seconds at 50.67 Hz: the rate at
 * which a catch cleared thresholds that had adapted to that half-volt swing,
 * on a floor otherwise two converter codes wide. A toggle leaves the level
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
 * ONE THING UM2408 DOES NOT SAY, AND THE DATASHEET SINCE HAS. Its footnote list
 * gives PB3 as D23/SWO and says nothing about PB4, so this file recorded the
 * JTAG question as unread. It was read on Tuesday 20 October 2026: DS13195 Rev 1
 * page 66 prints the pin as PB4(NJTRST), which is the function it carries after
 * reset. It does not affect the debugger in use: the ST-Link runs SWD, which
 * needs SWCLK on PA14, SWDIO on PA13 and SWO on PB3, and none of those is PB4.
 * The pin has been driven as a plain output through 60 185 measured edges since,
 * which is the answer in the other direction.
 *
 * AND ITS ALTERNATE FUNCTION 2 IS TIM3_CH1, from DS13195 Rev 1 Table 9 page 70,
 * read the same day. That is what makes the hardware marker possible at all and
 * it is the reason this file gained a second init below.
 *
 * THE WIRE IS NOT ON CN7 PIN 19 ANY MORE, and this file's table above is the Zio
 * position rather than the position in use. The X-NUCLEO-53L8A1 is stacked, so
 * CN7 pin 19 is underneath it and cannot be reached. PB4 is also Morpho CN12
 * pin 27, from UM2408 Rev 6 Table 19 page 47, and that is where the lead to the
 * witness sits. The connector positions and the method that resolved them are in
 * docs/the-board-and-the-wiring.md section 4.4; the port and pin here are
 * unchanged by any of it, which is the point of recording a port pin rather than
 * a connector position in code.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "marker.h"

#include "stm32h7xx.h"   /* CMSIS device header for this part, from the pack */

/* PB4, CN7 pin 19, Arduino D25. UM2408 Rev 6 Table 18 page 44, read Tuesday
 * 6 October 2026. The value is unchanged from the guess it replaces, which is
 * why the citation matters more than the number. */
#define MARKER_GPIO  GPIOB
#define MARKER_BIT   4u

/* Alternate function 2 on this pin is TIM3 channel 1, from DS13195 Rev 1
 * Table 9 page 70, read Tuesday 20 October 2026. The table is the part's own
 * alternate function mapping, not the board's, so it is a silicon fact and
 * survives any change of connector or shield. */
#define MARKER_AF_TIM3_CH1   2u
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

/* The same pin, handed to TIM3 channel 1. See marker.h for who calls this and
 * why it is not a back end's job.
 *
 * MODER 10 is the alternate function mode, and WHICH alternate function comes
 * from AFR: PB4 is in the low register, four bits per pin, so the field for pin
 * 4 starts at bit 16. Alternate function 2 is TIM3_CH1 on this pin, from
 * DS13195 Rev 1 Table 9 page 70.
 *
 * THE OTHER THREE SETTINGS ARE THE SAME THREE marker_init() USES and they are
 * repeated rather than shared, because the shared version would be a function
 * that configures a pin for either mode and the one line that differs between
 * them is the one worth seeing. Push-pull, no pull, and the fastest slew the pin
 * offers: slew matters here and only here, because the witness interpolates the
 * crossing, so a slow edge widens the uncertainty on every edge time in a run.
 *
 * THE PAD IS SWITCHED LAST BY ITS CALLER, after the channel is configured and
 * enabled, so the pin is never handed to a peripheral that has not been told
 * what to drive. */
bool marker_init_tim3_ch1(void)
{
    MARKER_RCC_ENABLE();

    const uint32_t pos2 = MARKER_BIT * 2u;
    const uint32_t pos4 = MARKER_BIT * 4u;

    MARKER_GPIO->AFR[0]  = (MARKER_GPIO->AFR[0] & ~(0xFu << pos4))
                           | (MARKER_AF_TIM3_CH1 << pos4);
    MARKER_GPIO->OTYPER &= ~(1u << MARKER_BIT);
    MARKER_GPIO->PUPDR  &= ~(3u << pos2);
    MARKER_GPIO->OSPEEDR |= (3u << pos2);

    /* MODER LAST OF THE FIVE, and that ordering is the one thing in this
     * function that is not a copy. The pin leaves input mode only once the
     * alternate function number, the drive type, the pull and the slew are all
     * in place, so there is no window in which the pad is an output driven by an
     * unselected function. marker_init() has no such ordering question, because
     * a plain output has nothing to select. */
    MARKER_GPIO->MODER   = (MARKER_GPIO->MODER & ~(3u << pos2)) | (2u << pos2);

    /* AND NOW READ BOTH BACK, WHICH THIS FUNCTION DID NOT DO UNTIL Monday
     * 26 October 2026 AND SHOULD HAVE FROM THE START.
     *
     * The first witness run against the hardware marker came back at 49.98 Hz
     * with 583 millivolts of swing, which is the mains on an open input, and the
     * console could not help: it reported the timer running, the channel's mode
     * and output enable both reading back, the trigger as TIM3's and 59 520
     * samples delivered, and said NOTHING about the two registers that decide
     * whether the channel reaches the pad. Every other configuration register in
     * this build is read back and reported. These two were the exception, and
     * they are the last two before the signal leaves the die.
     *
     * WHAT IS CHECKED AND WHAT IS NOT. The mode field must hold 10, alternate
     * function, and the function field must hold 2, TIM3_CH1. Those two decide
     * whether there is an edge at all. The drive type, the pull and the slew are
     * written and not reported, because they shape an edge rather than create
     * one; if a later run needs to argue about edge speed, OSPEEDR joins these
     * two and the line is cheap. */
    const bool mode_ok = (MARKER_GPIO->MODER & (3u << pos2)) == (2u << pos2);
    const bool func_ok = (MARKER_GPIO->AFR[0] & (0xFu << pos4))
                         == (MARKER_AF_TIM3_CH1 << pos4);

    printf("  marker pad mode            %s  wrote %08lX  read back %08lX\r\n",
           mode_ok ? "ok     " : "REFUSED",
           (unsigned long) (2u << pos2), (unsigned long) MARKER_GPIO->MODER);
    printf("  marker pad function 2      %s  wrote %08lX  read back %08lX\r\n",
           func_ok ? "ok     " : "REFUSED",
           (unsigned long) (MARKER_AF_TIM3_CH1 << pos4),
           (unsigned long) MARKER_GPIO->AFR[0]);

    return mode_ok && func_ok;
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
