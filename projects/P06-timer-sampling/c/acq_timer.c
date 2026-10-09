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
#include "acq_errors.h"
#include "marker.h"

#include "adcmath.h"
#include "cyccnt.h"
#include "pwmmath.h"

#include "stm32h7xx.h"

#include <stdio.h>

/* Reached the way main.c reaches board_init, by declaration rather than by
 * include, because CMake puts only this project's c/ and c/instr on this
 * target's include path and the board support lives in neither. */
extern uint32_t board_pclk1_hz(void);

/* Why acq_start refused, as distinct values rather than one. THESE MOVED to
 * acq_errors.h on Friday 9 October 2026, and the reason is in that header: they
 * lived here, so the other two back ends behind the same interface improvised
 * their own, and the numbering collided. -3 was this file's APB1 prescaler and
 * the dma back end's "transfer engine not configured" at the same time. */

/* THE CLOCK MODE, WHICH IS A DECISION RATHER THAN A READING, and these two lines
 * are the whole of it so that revisiting it is a two line edit.
 *
 * ST does not choose between synchronous and asynchronous either;
 * ADC_ConfigureBoostMode branches on it at stm32h7xx_hal_adc.c line 3942, taking
 * rcc_hclk1 divided by CKMODE in one case and a dedicated kernel clock reached
 * through RCC_PERIPHCLK_ADC in the other. So this is ours to pick and to justify.
 *
 * SYNCHRONOUS, DIVIDING BY FOUR, for three reasons and with its cost named.
 * It adds no register this volume has not sourced: the asynchronous route needs
 * a dedicated kernel clock configured and sourced first, which is a second
 * unread question stacked on this one. Divide by four is the SLOWEST ratio
 * CKMODE can express, which is the most conservative choice available while this
 * part's maximum ADC kernel clock is still a datasheet question, and that
 * datasheet is recorded as unread in docs/the-board-and-the-wiring.md. And the
 * resulting clock is one adcmath accepts rather than refuses.
 *
 * The cost is conversion rate. At the 64 MHz reset clock this gives a 16 MHz
 * kernel clock where synchronous divide by one would give 64 MHz, so a
 * conversion takes four times as long as it need. For 1 kHz sampling that is
 * irrelevant by three decades, and when it stops being irrelevant the fix is to
 * read the datasheet rather than to raise this number hopefully.
 *
 * THE FIELD VALUE IS 3 AND IT WAS READ, not inferred from the bit count. ST's
 * stm32h7xx_ll_adc.h defines LL_ADC_CLOCK_SYNC_PCLK_DIV4 as
 * (ADC_CCR_CKMODE_1 | ADC_CCR_CKMODE_0), both bits of a two bit field at bit 16,
 * and LL_ADC_CLOCK_ASYNC_DIV1 as 0. Read Wednesday 7 October 2026. The
 * expectation was written down before the header was opened and the header
 * agreed, which is the only reason it is written as a literal here. */
#define ADC_CKMODE_SYNC_DIV4      3u    /* the ADC_CCR_CKMODE field value */
#define ADC_CKMODE_DIVISOR        4u    /* what that value divides HCLK by */

/* Bounded, because nothing in this file waits forever. Calibration on this part
 * is tens of ADC clock cycles and the ready flag follows within a few more, so
 * these are both orders of magnitude of headroom rather than tuned numbers. */
#define ADC_CAL_POLLS_MAX     1000000u
#define ADC_READY_POLLS_MAX   1000000u

/* THE CHANNEL AND THE PIN, FROM ST FOR THIS EXACT BOARD, settled Wednesday
 * 7 October 2026 from the installed pack rather than from the datasheet, which
 * docs/the-board-and-the-wiring.md still records as unread.
 *
 * Projects/NUCLEO-H7A3ZI-Q/Examples/ADC/ADC_DualModeInterleaved of
 * STM32Cube_FW_H7_V1.13.0 states it twice, which is why this is settled rather
 * than corroborated:
 *
 *   readme.txt:83   "ADC_CHANNEL_13 on pin PC.03 (Arduino connector CN9 pin 5,
 *                   Morpho connector CN11 pin 37)"
 *   Inc/main.h:58   ADCx_CHANNELa ADC_CHANNEL_13, _GPIO_PORT GPIOC,
 *                   _PIN GPIO_PIN_3
 *
 * One sentence giving the channel, the port pin and both connector positions is
 * the same completeness that settled PE13 from Examples/TIM/TIM_DMA, from the
 * same authority. That example drives channel 13 from BOTH converters, so the
 * channel reaches ADC1, which is the one this file uses.
 *
 * AND PC3 COLLIDES WITH NOTHING. Not LD1 on PB0, LD2 on PE1, LD3 on PB14, the
 * button on PC13, the console on PD8 and PD9, the counting input on PD12, the
 * signal source on PE13, the marker on PB4, or SWDIO, SWCLK and SWO on PA13,
 * PA14 and PB3. */
#define ADC_CHANNEL_NUMBER      13u    /* PC3, CN9 pin 5 */

/* THE SAMPLING TIME IS A CHOICE AND THE EXAMPLES DISAGREE, so it is recorded as
 * a choice with its arithmetic rather than copied from whichever file was opened
 * first. Of the five ADC examples for this board, three use 810.5 cycles, one
 * uses 2.5 and calls it the minimum, and one uses 8.5.
 *
 * 810.5 is taken, for two reasons and with its cost named. It is the majority
 * choice, and a long sampling window is the tolerant one: a short window demands
 * a low source impedance, and what will eventually be connected to PC3 is not
 * decided. The cost is time, and the arithmetic that has to hold for this
 * project's claim is this: at the 16 MHz kernel clock configured above, 810.5
 * sampling cycles plus a 16-bit conversion is roughly 51 microseconds, which is
 * about five per cent of a 1000 microsecond period. So 1 kHz is reachable with
 * room to spare, and that is the number worth checking rather than the setting.
 *
 * WHAT IS CONNECTED TO PC3 TODAY IS NOTHING, which is worth saying plainly. The
 * converted value is therefore meaningless and this project's subject is the
 * RATE rather than the value. A real source would want the minimum sampling time
 * its impedance allows, which is a datasheet question this has not read.
 *
 * The field value 7 is ST's: LL_ADC_SAMPLINGTIME_810CYCLES_5 is all three bits
 * of an SMP field, and for channel 13 that field is SMP13 at bit 9 of SMPR2. */
#define ADC_SMP_810CYCLES_5      7u

/* THE REMAINING FIELD VALUES, each read rather than inferred, Wednesday
 * 7 October 2026.
 *
 * EXTSEL 13 for TIM6 TRGO, which this repository already carried from a line
 * number and which is now confirmed by decoding ST's own composition:
 * LL_ADC_REG_TRIG_EXT_TIM6_TRGO is (EXTSEL_3 | EXTSEL_2 | EXTSEL_0), and
 * 8 + 4 + 1 is 13.
 *
 * EXTEN 1 for the rising edge, from LL_ADC_REG_TRIG_EXT_RISING being EXTEN_0
 * alone. This was the last value on the project's open list.
 *
 * RES 0 for 16 bits, from LL_ADC_RESOLUTION_16B being 0x00000000. All five of
 * this board's ADC examples use 16 bits, which is also this part's native width,
 * so there is no choice here to record. It is the reset value, and it is written
 * explicitly anyway so the intent is in the code rather than in the silicon.
 *
 * And a sequence of one conversion, so SQR1's length field is 0 and SQ1 holds
 * the channel. ST's own comment is that L counts conversions minus one. */
#define ADC_EXTSEL_TIM6_TRGO    13u
#define ADC_EXTEN_RISING         1u
#define ADC_RES_16BIT            0u
#define ADC_SQR1_ONE_CONVERSION  0u

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

/* tADCVREG_STUP, the internal regulator start-up time, from
 * stm32h7xx_ll_adc.h line 1537 where ST names it
 * LL_ADC_DELAY_INTERNAL_REGUL_STAB_US. ST's own comment calls for a DELAY rather
 * than a poll, and this image keeps the delay.
 *
 * AND WHAT THIS COMMENT USED TO SAY WAS WRONG, which is worth more than the
 * correction. It said "there is no ready flag for this one, so there is nothing
 * to poll." THERE IS A FLAG. ADC_ISR_LDORDY, bit 12, mask 0x00001000, which the
 * device header for this exact part describes as the "ADC LDO output voltage
 * ready bit". ST references it six times in its own driver headers,
 * stm32h7xx_hal_adc.h at 913 and 1376 and stm32h7xx_ll_adc.h at 655, 7546, 7550
 * and 7552, and uses it NOWHERE in the .c files that implement the start-up.
 *
 * THE ERROR WAS INFERRING THE ABSENCE OF A FLAG FROM ST'S CHOICE NOT TO POLL
 * ONE, which is the same shape as reading a value off a schematic and calling it
 * a meaning. Reference code shows one working sequence; the register list shows
 * what the part can do. Those are different questions and only the second was
 * ever worth asking here. The flag was in the device header the whole time, and
 * this file had already PRINTED it without recognising it: the enable step's
 * read-back of 00001001 is ADRDY at bit 0 and LDORDY at bit 12, and that unknown
 * bit 12 sat named as an open item in three files for a day.
 *
 * THE DELAY STAYS AND THE FLAG IS REPORTED BESIDE IT, deliberately, and the
 * sequence does not gate on the flag. ST's delay is the documented sufficient
 * condition and it is met; the flag is new to this repository and has been
 * observed exactly once. Gating on a bit this file learned about today would
 * stake the whole bring-up on one observation. If it reads set on every run for
 * a while, promoting it to a bound poll is a two line change and then the delay
 * becomes the fallback, which is the shape the ADEN erratum settled into. */
#define ADC_VREG_STARTUP_US  10u

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
     * PULSE HERE: the marker must carry one edge per CONVERSION and not one per
     * interrupt, or the witness would measure this handler's entries rather than
     * the sampling instants, which is the one thing it exists to measure. */
    if ((isr & ADC_ISR_OVR) != 0u) {
        conv_overruns++;
        ADC1->ISR = ADC_ISR_OVR;
    }

    if ((isr & ADC_ISR_EOC) == 0u) {
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

/* The APB1 prescaler as a divisor, from the three encodings this volume has
 * sourced. Anything else returns 0, which pwmmath_timer_hz then refuses, and
 * that refusal is correct rather than unfortunate: reporting an undivided
 * frequency here would be wrong by exactly the ratio nobody would suspect.
 *
 * This is c/instr/pwmsrc.c's apb2_divisor with CDPPRE1 in place of CDPPRE2,
 * read from the sibling before writing this rather than derived again. The
 * field values are 0 for divide by one, 4 for two and 5 for four. */
static uint32_t apb1_divisor(void)
{
    const uint32_t field =
        (RCC->CDCFGR2 & RCC_CDCFGR2_CDPPRE1) >> RCC_CDCFGR2_CDPPRE1_Pos;
    switch (field) {
    case 0x0u: return 1u;
    case 0x4u: return 2u;
    case 0x5u: return 4u;
    default:   return 0u;
    }
}

/* A delay in microseconds, measured on the cycle counter rather than counted in
 * loop iterations.
 *
 * The cycle counter is the instrument this project already verified at boot:
 * cyccnt_init enables it and checks that it moved, and cyccnt_tick_hz refuses
 * with 0 when the rate could not be established. So a delay here is either
 * measured or declined, and never a loop whose duration nobody knows. Returns
 * false when there is no rate, which the caller turns into a refusal: a
 * regulator start-up wait of unknown length is not a wait. */
static bool delay_us(uint32_t us)
{
    const uint32_t hz = cyccnt_tick_hz();
    if (hz == 0u) {
        return false;
    }

    /* 64-bit so the multiply cannot wrap, and rounded up so the delay is never
     * shorter than asked. At 280 MHz, 10 microseconds is 2800 ticks, far inside
     * one wrap of the 32-bit counter. */
    const uint64_t want = (((uint64_t) hz * (uint64_t) us) + 999999u) / 1000000u;
    const uint32_t ticks = (uint32_t) want;
    const uint32_t t0 = cyccnt_now();

    while ((cyccnt_now() - t0) < ticks) {
        /* Unsigned subtraction is correct across one wrap. */
    }
    return true;
}

/* One line per step, in the shape clock280.c settled on: what was intended and
 * what the register actually holds, both printed, because that pair is what
 * turns a failed bring-up into a readable fault. A wrong peripheral base reads
 * back the reset value, a reserved field reads back zero, and a field that moved
 * between parts reads back something that is neither. */
static void report(const char *step, uint32_t wrote, uint32_t read_back, bool ok)
{
    printf("  adc %-22s %s  wrote %08lX  read back %08lX\r\n",
           step, ok ? "ok     " : "REFUSED",
           (unsigned long) wrote, (unsigned long) read_back);
}

/* The converter bring-up, all eight steps of the project README, complete since
 * Wednesday 7 October 2026. Every step reports what it wrote and what the
 * register holds, because that pair is what turns a silent misconfiguration into
 * a readable one.
 *
 *   1  leave deep power-down          stm32h7xx_ll_adc.h:6823
 *   2  the internal regulator on      :6856
 *   3  wait ten microseconds          :1537, tADCVREG_STUP, no flag to poll
 *   4  the kernel clock and boost     stm32h7xx_hal_adc.c:3939 to :4008
 *   5  calibrate, then enable         :3713 to :3716, and the erratum there does
 *                                     not bite in this image: see the single
 *                                     write below
 *   6  the preselection bit           :2905, through adcmath_pcsel_bit
 *   7  the trigger and the resolution CFGR, EXTSEL 13 and EXTEN 1
 *   8  the sampling time and rank     SMPR2 and SQR1
 *
 * THE LAST THING TO COME OFF THE OPEN LIST WAS THE CHANNEL, and it came off
 * without the datasheet: ST's own ADC example for this exact board names
 * ADC_CHANNEL_13 on PC3 twice, in its readme and in its board support defines.
 * That is the same authority and the same completeness that settled PE13 and
 * PD12, so the four values this file needed are sourced rather than chosen, with
 * one exception recorded as a choice above, the sampling time.
 *
 * THE ORDER IS ST'S AND IS NOT REARRANGED FOR CONVENIENCE. Boost precedes
 * calibration, because calibrating at one boost setting and running at another
 * calibrates the analogue path for conditions it will not see. The regulator
 * precedes both, and its ten microsecond wait is a delay rather than a poll
 * because that is what ST's sequence does. NOT because no flag exists: LDORDY at
 * bit 12 of ISR is exactly that flag, this file wrongly said it did not exist,
 * and the constant's own comment above now carries the correction and the reason
 * the delay is kept anyway.
 *
 * ONE DEPARTURE FROM THE README'S NUMBERING, and it is deliberate: step 8 is
 * written before step 7. The channel's sampling time and its place in the
 * sequence are what a conversion needs; the trigger is what starts one.
 * Configuring the trigger last means no edge can arrive before the thing it
 * would start is ready. */
static int adc_bring_up(uint32_t pclk1_hz, uint32_t divisor)
{
    /* The peripheral's bus clock first, for the reason freqcount_init gives:
     * every register write below goes nowhere without it, and goes nowhere
     * silently. Read back, because the write is posted. */
    RCC->AHB1ENR |= RCC_AHB1ENR_ADC12EN;
    (void) RCC->AHB1ENR;

    /* AS FOUND, BEFORE ANYTHING BELOW TOUCHES IT, which is the one thing the
     * first run of this file could not say.
     *
     * On Wednesday 7 October 2026 step 1 printed "wrote 00000000 read back
     * 00000000", and that is equally consistent with DEEPPWD having been set at
     * reset and cleared correctly and with DEEPPWD having been clear already. So
     * that run could not claim deep power-down was EXITED, only that the bit
     * read clear afterwards, and the pages said exactly that rather than the
     * stronger thing.
     *
     * This is the same correction c/board/clock280.c already made: it prints
     * every register before touching any of them, and its dump was relabelled
     * "as found" rather than "at reset" once a RESET press turned out not to
     * produce reset values. A step that reports only its own write can say what
     * the register holds and never what it changed. */
    const uint32_t cr_as_found = ADC1->CR;
    printf("  adc %-22s as found %08lX  DEEPPWD %u  ADVREGEN %u\r\n",
           "control register", (unsigned long) cr_as_found,
           (unsigned) ((cr_as_found & ADC_CR_DEEPPWD) != 0u),
           (unsigned) ((cr_as_found & ADC_CR_ADVREGEN) != 0u));

    /* Step 1, from stm32h7xx_ll_adc.h line 6823. Leave deep power-down AND
     * force the seven read-set bits to their reset state in the written value,
     * which is what ST does and is not the same as a read-modify-write. */
    const uint32_t cr1 = ADC1->CR & ~(ADC_CR_DEEPPWD | ADC_CR_READ_SET_BITS);
    ADC1->CR = cr1;
    const bool deeppwd_clear = (ADC1->CR & ADC_CR_DEEPPWD) == 0u;
    report("leave deep power-down", cr1, ADC1->CR, deeppwd_clear);
    if (!deeppwd_clear) {
        return ACQ_ERR_ADC_DEEPPWD;
    }

    /* Step 2, from line 6856. The regulator on, the same way. */
    const uint32_t cr2 = (ADC1->CR & ~ADC_CR_READ_SET_BITS) | ADC_CR_ADVREGEN;
    ADC1->CR = cr2;
    const bool vreg_set = (ADC1->CR & ADC_CR_ADVREGEN) != 0u;
    report("regulator enable", cr2, ADC1->CR, vreg_set);
    if (!vreg_set) {
        return ACQ_ERR_ADC_VREG;
    }

    /* Step 3, from line 1537. Ten microseconds on the cycle counter. */
    if (!delay_us(ADC_VREG_STARTUP_US)) {
        report("regulator start-up", ADC_VREG_STARTUP_US, 0u, false);
        return ACQ_ERR_NO_CYCLE_COUNTER;
    }
    report("regulator start-up", ADC_VREG_STARTUP_US, ADC1->CR, true);

    /* AND THE FLAG THIS FILE SPENT A DAY SAYING DID NOT EXIST, read straight
     * after the delay and printed rather than gated on. See the comment at
     * ADC_VREG_STARTUP_US for why it is an observation and not yet a condition.
     *
     * This is a plain printf rather than a report() call on purpose: report()
     * prints REFUSED for a false result, and a sequence that prints REFUSED and
     * then carries on would be worse than one that does not mention the flag.
     * The OVRMOD line later in this file is printed the same way for the same
     * reason.
     *
     * WHAT TO WATCH FOR. Set is expected, because ST's documented start-up time
     * has just elapsed. CLEAR WOULD BE THE INTERESTING RESULT and it would mean
     * one of two things: the delay is not actually sufficient on this part, or
     * this bit does not mean what the header says. Either is worth a flash of its
     * own, and neither is a reason to stop a sequence whose documented condition
     * was met. */
    printf("  adc regulator ready flag   %s  isr %08lX  (LDORDY, bit 12)\r\n",
           ((ADC1->ISR & ADC_ISR_LDORDY) != 0u) ? "set  " : "CLEAR",
           (unsigned long) ADC1->ISR);

    /* Step 4, boost mode, from stm32h7xx_hal_adc.c lines 3939 to 4008.
     *
     * HCLK IS RECONSTRUCTED RATHER THAN FETCHED, and the arithmetic is exact by
     * definition rather than by approximation: PCLK1 is HCLK divided by CDPPRE1,
     * so HCLK is PCLK1 multiplied by that same divisor. Both terms are already
     * in hand and verified, so this needs no new board accessor. c/clock's
     * decode computes the AHB frequency too, and board.h does not publish it;
     * adding an accessor to shared board support for one caller would be the
     * larger change.
     *
     * EVERY REFUSAL HERE IS adcmath's, which is the point of that file existing.
     * The arithmetic is checked against twenty three hand written cases on a
     * laptop that compiles nothing for this part, so a wrong boost threshold or
     * an inexpressible divisor is caught there rather than by a converter that
     * samples slightly wrongly. */
    const uint32_t hclk = pclk1_hz * divisor;
    uint32_t adc_clk = 0u;
    if (!adcmath_clock_hz(hclk, ADC_CKMODE_DIVISOR, &adc_clk)) {
        report("kernel clock", hclk, 0u, false);
        return ACQ_ERR_ADC_CLOCK;
    }

    uint32_t boost = 0u;
    if (!adcmath_boost(adc_clk, &boost)) {
        /* Above adcmath's own 100 MHz ceiling, which is this repository's and
         * not ST's: ST would select 3 and ask nothing. A caller arriving here
         * has an arithmetic error upstream rather than a fast converter. */
        report("boost from clock", adc_clk, 0u, false);
        return ACQ_ERR_ADC_BOOST;
    }

    /* The clock mode goes in the COMMON block and not in ADC1. Same ADC12EN
     * clock enable, so nothing further is needed to reach it. */
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~ADC_CCR_CKMODE)
                      | (ADC_CKMODE_SYNC_DIV4 << ADC_CCR_CKMODE_Pos);
    const bool ckmode_ok =
        ((ADC12_COMMON->CCR & ADC_CCR_CKMODE) >> ADC_CCR_CKMODE_Pos)
        == ADC_CKMODE_SYNC_DIV4;
    report("clock mode sync /4", ADC_CKMODE_SYNC_DIV4, ADC12_COMMON->CCR,
           ckmode_ok);
    if (!ckmode_ok) {
        return ACQ_ERR_ADC_CLOCK;
    }

    ADC1->CR = (ADC1->CR & ~(ADC_CR_READ_SET_BITS | ADC_CR_BOOST))
             | (boost << ADC_CR_BOOST_Pos);
    const bool boost_ok =
        ((ADC1->CR & ADC_CR_BOOST) >> ADC_CR_BOOST_Pos) == boost;
    report("boost mode", boost, ADC1->CR, boost_ok);
    if (!boost_ok) {
        return ACQ_ERR_ADC_BOOST;
    }
    printf("      kernel clock %lu Hz from HCLK %lu over %u, boost %lu\r\n",
           (unsigned long) adc_clk, (unsigned long) hclk,
           (unsigned) ADC_CKMODE_DIVISOR, (unsigned long) boost);

    /* Step 5a, calibration. ADCAL is a read-set bit, so it is SET by writing a
     * one and the hardware clears it when the calibration finishes.
     *
     * OFFSET ONLY, SINGLE ENDED, and both halves of that are choices. ADCALDIF
     * at bit 30 selects differential calibration and is left clear, because the
     * channel this project will sample is single ended. ADCALLIN at bit 16 adds
     * linearity calibration alongside offset, which ST offers as a separate
     * mode, and it is deliberately NOT done here: offset calibration is what a
     * first conversion needs, and adding linearity now would mean two things
     * were new at once if the result disappoints. It is one bit when wanted. */
    ADC1->CR = (ADC1->CR & ~(ADC_CR_READ_SET_BITS | ADC_CR_ADCALDIF
                             | ADC_CR_ADCALLIN))
             | ADC_CR_ADCAL;

    uint32_t polls = 0u;
    while ((ADC1->CR & ADC_CR_ADCAL) != 0u && polls < ADC_CAL_POLLS_MAX) {
        polls++;
    }
    const bool cal_ok = (ADC1->CR & ADC_CR_ADCAL) == 0u;
    report("calibration", ADC_CR_ADCAL, ADC1->CR, cal_ok);
    printf("      calibration took %lu polls\r\n", (unsigned long) polls);
    if (!cal_ok) {
        return ACQ_ERR_ADC_CALIBRATION;
    }

    /* Step 5b, THE ENABLE, AND THE OBVIOUS IMPLEMENTATION IS WRONG.
     *
     * From stm32h7xx_hal_adc.c lines 3713 to 3716, which is an erratum
     * workaround in ST's own words: if ADEN is set less than four ADC clock
     * cycles after the ADCAL bit, continue setting ADEN until ADRDY becomes 1.
     *
     * So one write to ADEN followed by a poll of ADRDY is not enough. The enable
     * has to be RE-ASSERTED inside the wait, which is what this loop does: every
     * pass writes ADEN again and then looks at ADRDY. A converter that starts
     * most of the time is worse than one that refuses, because the times it does
     * not start look like a wiring fault somewhere else entirely.
     *
     * ADEN is read-set, so each pass writes a one into it and the zeroes
     * elsewhere in the mask change nothing. */
    /* THE SINGLE WRITE IS TRIED FIRST, AND IT IS THE EXPERIMENT.
     *
     * On Wednesday 7 October 2026 the re-asserting loop reported four passes,
     * and that number was read as the erratum biting. It does not show that.
     * The loop writes ADEN and polls ADRDY on every pass, so four passes fits
     * two stories equally: the first three writes were ignored and the fourth
     * took, or the FIRST write took and the flag simply needed four passes to
     * rise. Nothing in that report separates them.
     *
     * So this writes ADEN exactly once and then polls without touching CR
     * again. The outcome is the answer, and both outcomes are useful:
     *
     *   it becomes ready      the re-assertion is NOT required on this part.
     *                         The loop below stays as insurance and no chapter
     *                         may claim the erratum applies here
     *   it reaches the limit  the re-assertion IS required, the erratum bites,
     *                         and the loop below is what makes the converter
     *                         usable at all
     *
     * Either way the converter ends up enabled, which is why this is one flash
     * rather than two builds and a comparison. */
    ADC1->CR = (ADC1->CR & ~ADC_CR_READ_SET_BITS) | ADC_CR_ADEN;

    uint32_t single = 0u;
    while ((ADC1->ISR & ADC_ISR_ADRDY) == 0u && single < ADC_READY_POLLS_MAX) {
        single++;
    }
    bool ready = (ADC1->ISR & ADC_ISR_ADRDY) != 0u;
    report("enable, one write", ADC_CR_ADEN, ADC1->ISR, ready);
    if (ready) {
        printf("      ready after %lu polls with NO re-assertion, so the "
               "erratum did not bite here\r\n", (unsigned long) single);
    } else {
        printf("      not ready after %lu polls with no re-assertion, so the "
               "erratum DOES bite here\r\n", (unsigned long) single);
    }

    /* The workaround, run only if the single write was not enough. From
     * stm32h7xx_hal_adc.c lines 3713 to 3716, in ST's own words: if ADEN is set
     * less than four ADC clock cycles after the ADCAL bit, continue setting ADEN
     * until ADRDY becomes 1. ADEN is read-set, so each pass writes a one into it
     * and the zeroes elsewhere in the mask change nothing. */
    if (!ready) {
        polls = 0u;
        while ((ADC1->ISR & ADC_ISR_ADRDY) == 0u
               && polls < ADC_READY_POLLS_MAX) {
            ADC1->CR = (ADC1->CR & ~ADC_CR_READ_SET_BITS) | ADC_CR_ADEN;
            polls++;
        }
        ready = (ADC1->ISR & ADC_ISR_ADRDY) != 0u;
        report("enable, re-asserted", ADC_CR_ADEN, ADC1->ISR, ready);
        printf("      the enable was re-asserted %lu times\r\n",
               (unsigned long) polls);
    }

    if (!ready) {
        return ACQ_ERR_ADC_NOT_READY;
    }

    /* Step 6, and this is where it stops now.
     *
     * The preselection bit is 1 << channel and adcmath_pcsel_bit computes it,
     * refusing above channel 19. What is missing is WHICH CHANNEL, which is a
     * datasheet question: which converter input reaches which pin on this
     * package, and what sampling time that input needs. Steps 7 and 8 need the
     * same answer, plus the rising edge value for EXTEN.
     *
     * Guessing a channel would configure the converter to sample something, and
     * a plausible reading from the wrong input is the exact failure this project
     * exists to make impossible. */
    /* THE PIN FIRST, because every register below is correct and nothing reaches
     * the converter without it. PC3 to analog mode, which is MODER 11, and the
     * port's clock before that for the reason freqcount_init gives: the write
     * goes nowhere without it and goes nowhere silently. */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
    (void) RCC->AHB4ENR;

    const uint32_t moder_pos = 3u * 2u;        /* PC3, two bits per pin */
    GPIOC->MODER |= (3u << moder_pos);         /* 11, analog */
    const bool pin_analog =
        ((GPIOC->MODER >> moder_pos) & 3u) == 3u;
    report("PC3 to analog", 3u, GPIOC->MODER, pin_analog);
    if (!pin_analog) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    /* Step 6, the preselection bit, from stm32h7xx_hal_adc.c line 2905.
     *
     * The bit is 1 << channel and adcmath_pcsel_bit computes it, refusing above
     * channel 19. That refusal is not redundant: ST's own expression masks the
     * channel with 0x1F, so a request for 36 would preselect channel 4 with
     * nothing in any register to show it. */
    uint32_t pcsel = 0u;
    if (!adcmath_pcsel_bit(ADC_CHANNEL_NUMBER, &pcsel)) {
        report("preselection", ADC_CHANNEL_NUMBER, 0u, false);
        return ACQ_ERR_ADC_CHANNEL;
    }
    ADC1->PCSEL |= pcsel;
    const bool pcsel_ok = (ADC1->PCSEL & pcsel) != 0u;
    report("preselection", pcsel, ADC1->PCSEL, pcsel_ok);
    if (!pcsel_ok) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    /* Step 8 before step 7, deliberately: the channel's sampling time and its
     * place in the sequence are what a conversion needs, and the trigger is what
     * starts one. Configuring the trigger last means no edge can arrive before
     * the thing it would start is ready.
     *
     * SMP13 sits at bit 9 of SMPR2, which is the register for channels 10 to 19.
     * SQR1 holds the length in its low bits and the first rank at bit 6. */
    ADC1->SMPR2 = (ADC1->SMPR2 & ~(7u << ADC_SMPR2_SMP13_Pos))
                | (ADC_SMP_810CYCLES_5 << ADC_SMPR2_SMP13_Pos);
    const bool smp_ok =
        ((ADC1->SMPR2 >> ADC_SMPR2_SMP13_Pos) & 7u) == ADC_SMP_810CYCLES_5;
    report("sampling time", ADC_SMP_810CYCLES_5, ADC1->SMPR2, smp_ok);
    if (!smp_ok) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    ADC1->SQR1 = (ADC_SQR1_ONE_CONVERSION << ADC_SQR1_L_Pos)
               | (ADC_CHANNEL_NUMBER << ADC_SQR1_SQ1_Pos);
    const bool sqr_ok =
        ((ADC1->SQR1 >> ADC_SQR1_SQ1_Pos) & 0x1Fu) == ADC_CHANNEL_NUMBER
        && ((ADC1->SQR1 >> ADC_SQR1_L_Pos) & 0xFu) == ADC_SQR1_ONE_CONVERSION;
    report("sequence of one", ADC_CHANNEL_NUMBER, ADC1->SQR1, sqr_ok);
    if (!sqr_ok) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    /* Step 7, the trigger: resolution, which TRGO starts a conversion, and the
     * edge. All three live in CFGR.
     *
     * THESE BITS ARE WRITE PROTECTED WHILE A CONVERSION IS RUNNING and not
     * merely while the converter is enabled, and nothing here has started one,
     * so ADSTART is clear. That condition is the reason this works after the
     * enable rather than before it, and the read-back below is what would show
     * it if the reading were wrong: a protected register reports the old value
     * rather than refusing, which is exactly the failure this file's step
     * reports exist to make visible. */
    ADC1->CFGR = (ADC1->CFGR & ~(ADC_CFGR_RES | ADC_CFGR_EXTSEL
                                 | ADC_CFGR_EXTEN))
               | (ADC_RES_16BIT << ADC_CFGR_RES_Pos)
               | (ADC_EXTSEL_TIM6_TRGO << ADC_CFGR_EXTSEL_Pos)
               | (ADC_EXTEN_RISING << ADC_CFGR_EXTEN_Pos);

    const uint32_t cfgr = ADC1->CFGR;
    const bool trig_ok =
        ((cfgr >> ADC_CFGR_EXTSEL_Pos) & 0x1Fu) == ADC_EXTSEL_TIM6_TRGO
        && ((cfgr >> ADC_CFGR_EXTEN_Pos) & 3u) == ADC_EXTEN_RISING
        && ((cfgr >> ADC_CFGR_RES_Pos) & 7u) == ADC_RES_16BIT;
    report("trigger and 16 bits", ADC_EXTSEL_TIM6_TRGO, cfgr, trig_ok);
    if (!trig_ok) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    printf("      channel %u on PC3 at CN9 pin 5, 810.5 cycles, 16 bits,\r\n",
           (unsigned) ADC_CHANNEL_NUMBER);
    printf("      started by TIM6 TRGO on the rising edge\r\n");
    return 0;
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

    /* TIM6 at exactly 1 kHz, and the timer clock is now derived rather than
     * refused.
     *
     * THIS BLOCK USED TO RETURN -2 UNCONDITIONALLY, with a comment saying the
     * APB1 timer clock was unconfirmed and that whether the timer clock equals
     * the bus clock or twice it was the factor between 1 kHz and 2 kHz. That is
     * exactly the question pwmmath_timer_hz answers, from ST's own
     * stm32h7xx_hal_rcc_ex.h lines 3596 to 3605, sourced Tuesday 6 October 2026:
     * the timer kernel clock is min(HCLK, 2 x PCLK) with TIMPRE clear and
     * min(HCLK, 4 x PCLK) with it set. Fourteen cases of that rule are checked
     * on the host against a second derivation, so the factor is settled and no
     * longer guessed.
     *
     * Three things can still refuse, and each says which. */
    const uint32_t pclk1 = board_pclk1_hz();
    if (pclk1 == 0u) {
        /* The clock tree was not decoded, so there is no honest frequency to
         * plan against. */
        return ACQ_ERR_PCLK1_UNKNOWN;
    }

    /* TIMPRE read rather than assumed. Nothing in this repository writes the
     * bit, so it is whatever reset left, which is clear; reading it costs one
     * load and means this does not depend on that staying true. */
    const bool timpre = (RCC->CFGR & RCC_CFGR_TIMPRE) != 0u;
    const uint32_t timer_clk_hz = pwmmath_timer_hz(pclk1, apb1_divisor(), timpre);
    if (timer_clk_hz == 0u) {
        /* Either CDPPRE1 holds a ratio this volume has not sourced, or the
         * rule does not cover the combination. Refusing is correct: a build
         * that guessed the clock would produce a clean looking square wave at
         * the wrong rate, and the whole point of chapter 6 is that such a wave
         * is indistinguishable from a right one without an external witness. */
        return ACQ_ERR_APB1_PRESCALER;
    }

    printf("  tim6 clock            %lu Hz  (pclk1 %lu, /%lu, timpre %u)\r\n",
           (unsigned long) timer_clk_hz, (unsigned long) pclk1,
           (unsigned long) apb1_divisor(), (unsigned) timpre);

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

    /* The converter, which is what the trigger above exists to start. It
     * reports each step and refuses at the first input it does not have.
     *
     * This is attempted BEFORE the timer is enabled, so a refusal leaves TIM6
     * configured and stopped rather than emitting a trigger output that nothing
     * consumes. That is clock280.c's rule applied here: return without the
     * irreversible step, so the part is left in a state somebody can read. */
    const int adc_rc = adc_bring_up(pclk1, apb1_divisor());
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
     * arms it. A TIM6 trigger arriving at an unarmed converter does nothing.
     *
     * The stale flags are cleared first, because ISR is not reset by
     * configuration and a left-over EOC would make the first interrupt report a
     * sample that predates this call. Both flags are cleared by writing one. */
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_OVR;
    ADC1->IER |= ADC_IER_EOCIE;
    const bool eocie = (ADC1->IER & ADC_IER_EOCIE) != 0u;
    report("end of conversion irq", ADC_IER_EOCIE, ADC1->IER, eocie);
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
    report("overrun irq", ADC_IER_OVRIE, ADC1->IER, ovrie);
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
    report("armed for the trigger", ADC_CR_ADSTART, ADC1->CR, armed);
    if (!armed) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    TIM6->CR1 |= TIM_CR1_CEN;
    printf("  tim6 started, so conversions begin on its update event\r\n");
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
