/* c/instr/adcmath.h: the converter's arithmetic, callable on any host.
 *
 * WHY THIS IS A SEPARATE FILE, which is c/clock/clocktree.h's reason and
 * c/instr/freqmath.h's and c/instr/pwmmath.h's. P06's three acquisition back
 * ends dereference the converter, so nothing on a host can call them, so the
 * only test their arithmetic could ever have is to flash the board and believe
 * the number. The converter's bring-up has one piece of real arithmetic in it
 * and four decisions that depend on which part this is, and both belong
 * somewhere a table of cases can reach them.
 *
 * AND THE REASON IT IS FOUR DECISIONS RATHER THAN A TRANSCRIPTION, which is what
 * made this file worth existing. Reading ST's stm32h7xx_hal_adc.c on Tuesday
 * 6 October 2026 found four points where the correct write depends on part
 * identity, every one behind a preprocessor condition or a silicon revision
 * check, and every one of which would otherwise have produced code that
 * compiles, links, flashes and samples at the wrong rate or not at all:
 *
 *   PCSEL or PCSEL_RES0   PCSEL_RES0 sits behind #if defined(ADC_VER_V5_V90).
 *                         That macro does not appear in stm32h7a3xxq.h, so this
 *                         part writes plain PCSEL.
 *   the boost rule        two branches on #if defined(ADC_VER_V5_3), and that
 *                         macro IS defined, at stm32h7a3xxq.h line 2621, so the
 *                         threshold table below applies.
 *   a silicon revision    the HAL_GetREVID() <= REV_ID_Y rule is in the #else
 *                         of that same condition, which this part never
 *                         compiles. There is NO revision fork for the H7A3.
 *   the ADC clock mode    not a fork in ST's code but a configuration this
 *                         project chooses, and it decides which clock the boost
 *                         rule is given.
 *
 * NOTHING HERE READS A REGISTER OR A CLOCK. Every function is a pure function of
 * its arguments, which is what makes python/tests/test_adcmath.py an oracle
 * rather than a recording of whatever the board happened to do.
 *
 * WHAT IS NOT HERE, because it is a sequence rather than a calculation: the
 * order of the bring-up, the ten microsecond regulator delay, and the erratum
 * that requires ADEN to be re-asserted until ADRDY reads 1. Those are register
 * work and they live with the back ends. P06's README carries all eight steps
 * with the file and line each came from.
 */
#ifndef ADCMATH_H
#define ADCMATH_H

#include <stdbool.h>
#include <stdint.h>

/* The BOOST field for a given ADC kernel clock, or false to refuse.
 *
 * THE RULE, from ADC_ConfigureBoostMode in ST's stm32h7xx_hal_adc.c lines 3991
 * to 4008, which is the branch this part compiles. The clock is HALVED first and
 * then compared, and the halving is ST's, not a unit conversion:
 *
 *   freq = adc_clk_hz / 2
 *   0  when freq is at or below  6250000
 *   1  at or below              12500000
 *   2  at or below              25000000
 *   3  above that
 *
 * Returned through a pointer rather than as the value, because 0 IS a legal
 * BOOST setting and could not also mean refused. freqmath_mhz can return 0 for
 * a refusal because 0 Hz is not a frequency this instrument reports; 0 here is
 * the setting a slow ADC clock wants.
 *
 * Refuses a clock of 0, which is what a caller gets from a clock tree it could
 * not decode, and refuses above 100 MHz, which is past anything this part's ADC
 * accepts and therefore a caller's arithmetic error rather than a configuration.
 * The upper limit is this file's own judgement and is marked as such: ST's
 * function has no upper branch, it simply selects 3. */
bool adcmath_boost(uint32_t adc_clk_hz, uint32_t *boost);

/* The ADC kernel clock, from its source and the divisor that applies to it.
 *
 * TWO SOURCES AND THEY ARE NOT INTERCHANGEABLE, which is the fourth decision
 * above. ST's ADC_ConfigureBoostMode branches at line 3942: in SYNCHRONOUS clock
 * mode the source is rcc_hclk1 and the divisor comes from CKMODE; in
 * ASYNCHRONOUS mode the source is the dedicated kernel clock, which ST reaches
 * through RCC_PERIPHCLK_ADC, and the divisor comes from PRESC. APB2 is not the
 * source in either mode, which is worth saying because an earlier note in this
 * repository said the boost setting would be computed from board_pclk2_hz() and
 * that was wrong.
 *
 * This function does not choose the mode. It takes the source frequency the
 * caller has established for the mode it has chosen, and the divisor, and
 * divides. Refuses a zero source, and refuses a divisor that is not one the
 * fields can express: 1, 2 and 4 for CKMODE, and the even values 2 to 12 plus
 * 16, 32, 64, 128 and 256 for PRESC. A divisor outside both sets is a caller's
 * mistake, and dividing by it anyway would hide the mistake behind a plausible
 * clock. */
bool adcmath_clock_hz(uint32_t source_hz, uint32_t divisor, uint32_t *adc_clk_hz);

/* The PCSEL bit for one channel, or false to refuse.
 *
 * ST writes PCSEL |= 1 << (channel & 0x1F) at stm32h7xx_hal_adc.c line 2905,
 * where channel is the decimal channel number. The mask is in ST's expression
 * and is NOT a substitute for checking the range: masking 36 to 4 would select
 * the wrong channel in silence, so a channel above 19 is refused here rather
 * than wrapped. Nineteen is this part's highest external channel number and is
 * the one figure in this file that wants a datasheet rather than the pack; until
 * it has one it is marked as the conservative choice it is. */
bool adcmath_pcsel_bit(uint32_t channel, uint32_t *bit);

#endif /* ADCMATH_H */
