/* acq_errors.h: one numbering for acq_start's refusals, shared by all three
 * back ends.
 *
 * WHY THIS FILE EXISTS, AND IT IS NOT TIDINESS. Until Friday 9 October 2026 these
 * codes lived inside acq_timer.c, so the other two back ends behind the same
 * interface improvised their own and the numbering collided:
 *
 *   timer     -2 to -11, named and documented
 *   systick   -1, returned twice for two DIFFERENT causes
 *   dma       -3, meaning "the transfer engine is not configured"
 *
 * AND -3 IS ACQ_ERR_APB1_PRESCALER IN THE TIMER BACK END. One interface, one
 * number, two meanings, and main.c prints the number. A reader looking up -3
 * would find the APB1 prescaler and go looking at a clock that was never the
 * problem. That is the volume's own failure shape, a plausible wrong answer with
 * no tell, arrived at this time through an interface rather than a register.
 *
 * It was found by reading acq_systick.c before copying the converter bring-up
 * into it, which is the discipline the catalogue note calls reading the sibling
 * before writing the copy, and the thing it found was not the thing it was
 * looking for.
 *
 * WHY THE NUMBERING STARTS AT -2, which was never written down and is now a rule
 * rather than an accident. acq_conv_overruns() in acq.h already returns -1 for
 * "this back end does not watch the flag". Giving acq_start a -1 as well would put
 * the same number on two unrelated meanings in one interface, so -1 is reserved
 * and acq_start never returns it.
 *
 * -2 TO -11 ARE UNCHANGED, deliberately, because they are on the board, in three
 * captures and in several commit messages. Renumbering them to close a gap would
 * silently invalidate every capture already recorded.
 */
#ifndef ACQ_ERRORS_H
#define ACQ_ERRORS_H

/* Reserved: acq_conv_overruns() uses -1. acq_start never returns it. */

/* The clock and the converter, reached by whichever back end gets that far.
 * The clock280.c lesson applies here exactly: "the converter did not come up" is
 * not a finding anybody can act on, and "the regulator wrote 10000000 and read
 * back 0" is. */
#define ACQ_ERR_PCLK1_UNKNOWN     (-2)
#define ACQ_ERR_APB1_PRESCALER    (-3)
#define ACQ_ERR_NO_CYCLE_COUNTER  (-4)
#define ACQ_ERR_ADC_DEEPPWD       (-5)
#define ACQ_ERR_ADC_VREG          (-6)
#define ACQ_ERR_ADC_CLOCK         (-7)   /* adcmath_clock_hz refused */
#define ACQ_ERR_ADC_BOOST         (-8)   /* adcmath_boost refused */
#define ACQ_ERR_ADC_CALIBRATION   (-9)   /* ADCAL never cleared */
#define ACQ_ERR_ADC_NOT_READY     (-10)  /* ADRDY never set */
#define ACQ_ERR_ADC_CHANNEL       (-11)  /* a step 6 to 8 register did not take */

/* Back-end specific, from -12, so that adding one never disturbs the codes above
 * and so that a reader can tell from the number alone which half of the interface
 * refused. */
#define ACQ_ERR_CORE_HZ_UNKNOWN   (-12)  /* systick: board_core_hz returned 0, so
                                          * the rate was never established, which
                                          * is the whole subject of P06 */
#define ACQ_ERR_SYSTICK_RELOAD    (-13)  /* systick: SysTick_Config rejected the
                                          * reload, a different thing from the
                                          * rate being unknown */
#define ACQ_ERR_DMA_UNCONFIGURED  (-14)  /* dma: the transfer engine is not
                                          * configured, and refusing is correct */

#endif /* ACQ_ERRORS_H */
