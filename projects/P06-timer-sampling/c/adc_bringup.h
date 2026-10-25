/* adc_bringup.h: the converter bring-up, shared by all three back ends.
 *
 * MOVED OUT OF acq_timer.c ON Friday 9 October 2026 AND NOT REWRITTEN. The
 * sequence ran on the board from Wednesday 7 October 2026 and samples; it is
 * shared because the other two back ends need the same eight steps and three
 * copies of a sourced register sequence drift without anything noticing.
 *
 * ONLY THE TRIGGER IS A PARAMETER, because only the trigger differs between the
 * three mechanisms: the timer and transfer-engine builds start a conversion from
 * TIM3's update event, which was TIM6's until Sunday 25 October 2026 and moved
 * when the marker moved to a timer channel, and the tick build starts one in
 * software. Everything
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

/* AND EXTSEL 4 FOR TIM3 TRGO, read on Sunday 25 October 2026 and the value the
 * timer and transfer-engine builds now pass, because the marker moved to TIM3
 * channel 1 and one timer carries both the trigger and the evidence of it.
 *
 * THE SOURCE, named exactly, because this number cannot come from the manual.
 * RM0455 Rev 13 page 1028 enumerates EXTSEL only as "Event 0" to "Event 31" and
 * names no timer against any of them, so the mapping from a timer to a field
 * value exists in ST's software and nowhere in the reference manual. The header
 * is stm32h7xx_ll_adc.h from STMicroelectronics/stm32h7xx-hal-driver, branch
 * master, file Inc/stm32h7xx_ll_adc.h, line 984:
 *
 *   LL_ADC_REG_TRIG_EXT_TIM3_TRGO  (ADC_CFGR_EXTSEL_2 | ...EDGE_DEFAULT)
 *
 * EXTSEL_2 alone is bit 2 of the field, so the value is 4.
 *
 * AND THE READING IS CROSS-CHECKED AGAINST A NUMBER THIS PROJECT ALREADY HAS ON
 * THE BOARD, which is what makes the 4 trustworthy rather than merely sourced.
 * Line 993 of the same table gives TIM6 TRGO as (EXTSEL_3 | EXTSEL_2 | EXTSEL_0),
 * which is 8 + 4 + 1 and therefore 13: the value above, carried here since
 * Wednesday 7 October 2026 from a different reading and confirmed by samples
 * arriving. The same table read the same way reproduces a confirmed number, so
 * the file and the method are the right ones.
 *
 * ONE THING WAS CHECKED BEFORE TRUSTING THE LINE, because this part is the one
 * the family's material is usually wrong about. The whole trigger block, lines
 * 976 to 1009, carries no family conditional at all: the only #if inside it are
 * for TIM23 and TIM24, which this part does not have, and the neighbouring
 * channel block a few lines above DOES branch on ADC_VER_V5_V90 and on ADC3,
 * which is how a conditioned table looks when there is one. So this table is
 * family-wide and these two values are this part's.
 *
 * TIM3_CH4 is in the same table at 15, and is not used: the trigger comes from
 * the update event, so the channels are free and channel 1 drives the pin.
 *
 * AND ADC_EXTSEL_TIM6_TRGO ABOVE IS NOW PASSED BY NO BUILD, which is a reason to
 * explain it rather than to delete it. Every measurement in this project up to
 * and including Tuesday 20 October 2026 was taken with that value in CFGR: the
 * twelve console captures, the 390 blocks of Wednesday 7 October 2026, the
 * 60 185 edges of the timer build and the 120 960 samples of the transfer-engine
 * build. A reader holding one of those captures needs to know what 13 meant in
 * it, so the name stays with its citation. */
#define ADC_EXTSEL_TIM3_TRGO     4u

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
 * holding only the field value 4 cannot tell TIM3 TRGO from any other source,
 * and a reader of a capture taken before Sunday 25 October 2026 is holding a 13
 * for TIM6 TRGO instead. The words are what make the two captures comparable. */
int adc_bring_up(uint32_t pclk1_hz, uint32_t divisor,
                 uint32_t extsel, uint32_t exten, const char *trigger_desc);

/* One line per step, and SHARED because the steps a back end performs after the
 * bring-up returns have to print in the same shape as the eight before them. It
 * was static and called report; the name gains a prefix because a bare report()
 * in the global namespace of a project this small is a collision waiting to
 * happen. The rename changes no output. */
void adc_report(const char *step, uint32_t wrote, uint32_t read_back, bool ok);

/* The APB1 prescaler as a divisor, for the pclk1_hz and divisor pair above.
 *
 * THIS IS IN THE WRONG HOUSE AND THE DUPLICATION IT LEAVES IS NAMED RATHER THAN
 * HIDDEN. It reads an RCC prescaler and is not an ADC concept, so it carries no
 * adc_ prefix. c/instr/pwmsrc.c holds a static apb2_divisor() that is the same
 * function over the neighbouring field, and the two CANNOT be merged as they
 * stand: that one is written against this repository's own register header, with
 * RCC_CDCFGR2, _POS and the named encodings RCC_APBPRE_DIV1, DIV2 and DIV4, where
 * this one is written against ST's vendor device header, with RCC->CDCFGR2, _Pos
 * and the BARE LITERALS 0x0, 0x4 and 0x5. There are two register-naming worlds in
 * this repository and a shared helper has to pick one.
 *
 * The literals are a defect in their own right and are left because fixing them
 * requires the same decision: the names exist, at c/board/stm32h7a3_regs.h lines
 * 227 to 229, in a header P06's targets do not compile.
 *
 * So this is option A of three, taken Friday 9 October 2026: one copy for P06's
 * three back ends rather than a third private one, with no other project touched.
 * Option C, one shared reader for both prescalers in c/instr, is the right answer
 * and is a volume-wide decision about which register header is canonical, because
 * pwmsrc.c is linked into P01 and P02. It is deferred, not forgotten. */
uint32_t apb1_divisor(void);

#endif /* ADC_BRINGUP_H */
