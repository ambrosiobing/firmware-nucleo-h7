/* adc_bringup.h: the converter bring-up, shared by all three back ends.
 *
 * MOVED OUT OF acq_timer.c ON Friday 9 October 2026 AND NOT REWRITTEN. The
 * sequence ran on the board from Wednesday 7 October 2026 and samples; it is
 * shared because the other two back ends need the same eight steps and three
 * copies of a sourced register sequence drift without anything noticing.
 *
 * ONLY THE TRIGGER IS A PARAMETER, because only the trigger differs between the
 * three mechanisms: the timer and transfer-engine builds start a conversion from
 * TIM6's update event and the tick build starts one in software. Everything
 * else, the regulator, the clock mode, the boost, the calibration, the enable,
 * the channel and its sampling time, is the same converter in all three.
 */
#ifndef ADC_BRINGUP_H
#define ADC_BRINGUP_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx.h"

/* THE TRIGGER FIELD VALUES, read rather than inferred, Wednesday
 * 7 October 2026, and here rather than in the implementation because the CALLER
 * chooses between them.
 *
 * EXTSEL 13 for TIM6 TRGO, which this repository already carried from a line
 * number and which is now confirmed by decoding ST's own composition:
 * LL_ADC_REG_TRIG_EXT_TIM6_TRGO is (EXTSEL_3 | EXTSEL_2 | EXTSEL_0), and
 * 8 + 4 + 1 is 13.
 *
 * EXTEN 1 for the rising edge, from LL_ADC_REG_TRIG_EXT_RISING being EXTEN_0
 * alone. This was the last value on the project's open list.
 *
 * EXTEN 0 is the software trigger, which is what the tick build wants: with no
 * external edge selected, a conversion starts when ADSTART is written. It is
 * the reset value and is named here so that a back end passing it is making a
 * choice rather than leaving a field alone. */
#define ADC_EXTSEL_TIM6_TRGO    13u
#define ADC_EXTEN_RISING         1u
#define ADC_EXTEN_SOFTWARE       0u

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

/* The eight steps, in ST's order. Returns 0, or one of acq_errors.h's codes.
 *
 * pclk1_hz times divisor gives HCLK, from which the converter's kernel clock is
 * derived. extsel and exten select the trigger. trigger_desc is printed after
 * the trigger step and must name the mechanism in words, because a reader
 * holding only the field value 13 cannot tell TIM6 TRGO from any other source. */
int adc_bring_up(uint32_t pclk1_hz, uint32_t divisor,
                 uint32_t extsel, uint32_t exten, const char *trigger_desc);

/* One line per step, and SHARED because the steps a back end performs after the
 * bring-up returns have to print in the same shape as the eight before them. It
 * was static and called report; the name gains a prefix because a bare report()
 * in the global namespace of a project this small is a collision waiting to
 * happen. The rename changes no output. */
void adc_report(const char *step, uint32_t wrote, uint32_t read_back, bool ok);

#endif /* ADC_BRINGUP_H */
