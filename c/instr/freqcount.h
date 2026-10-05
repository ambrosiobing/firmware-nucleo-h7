/* freqcount.h: a gated frequency counter, built on the board.
 *
 * One timer counts edges of the signal under test in external clock mode. A
 * second timer opens and closes a gate of known length. Count divided by gate
 * gives the frequency, and the resolution is one count in the gate, so a one
 * second gate on a 1 kHz signal resolves 1 Hz, which is 0.1 percent. That is
 * exactly at the tolerance the method document sets, which is why this
 * instrument checks the witness rather than replacing it.
 *
 * The reciprocal upgrade measures the period of one cycle instead of counting
 * cycles in a window, and its resolution is the timer clock rather than the
 * gate, so it is better by several orders. It needs an input capture channel.
 *
 * TWO 32-BIT TIMERS EXIST ON THIS PART and chapter 6 was going to spend one of
 * them here, which is why chapter 20 is careful about who owns it. Both of the
 * notes below overtake that: the gate moved to the crystal and the counter moved
 * to LPTIM1, so this instrument now costs chapter 20 no 32-bit timer at all.
 *
 * THE GATE CHANGED ON SUNDAY 4 OCTOBER 2026, before any of this was written, and
 * the reason is a measurement. The design above spends a SECOND timer on the
 * gate, clocked from the core. That core clock is now measured at 1168 parts per
 * million below its nominal, because the debugger's 8 MHz is 7990652 Hz, so a
 * core-clocked gate would carry that error into every frequency this instrument
 * reports, and would carry a different error at each of the two clocks the board
 * runs at.
 *
 * c/instr/lseref.c already gates on the 32.768 kHz crystal, by counting the real
 * time clock's sub second register, and that reference does not come from the
 * PLL chain. Using it here removes the second timer entirely and makes the
 * counter crystal-accurate rather than PLL-accurate. One timer in external clock
 * mode to count edges, and a gate that is already written.
 *
 * AND LATER THE SAME DAY THE COUNTER CHANGED TOO, because the unconfirmed list
 * was answered. Until Sunday 4 October 2026 this header said three things were
 * unknown: which peripheral can count an external signal on a pin this board
 * exposes, that pin's alternate function number, and the trigger selection
 * value. All three came out of ST's own source for this exact board, which is
 * the same authority that settled the console pins on Friday 2 October 2026 and
 * the 280 MHz dividers on Sunday 4 October 2026.
 *
 * The file is
 * STM32Cube_FW_H7_V1.13.0/Projects/NUCLEO-H7A3ZI-Q/Examples/LPTIM/
 * LPTIM_PulseCounter, and it is a working pulse counter for this board rather
 * than for the family:
 *
 *   what counts          LPTIM1, CounterSource = LPTIM_COUNTERSOURCE_EXTERNAL
 *   the pin              PD12, GPIO_AF1_LPTIM1, AF push-pull, pull-up, medium
 *                        speed, straight out of its HAL_LPTIM_MspInit
 *   the width            16 bits, so the overflow has to be counted in software
 *   the gate             lseref.c, already written, on the 32.768 kHz crystal
 *
 * IT IS NOT A 32-BIT TIMER AND THAT IS A REAL CHANGE, not a substitution. The
 * paragraph above still says two 32-bit timers exist and that chapter 6 spends
 * one of them. It does not need to any more. The 32-bit route is TIM2 or TIM5
 * with ETRSEL = 0 to select the GPIO, which is the trigger selection value that
 * was unknown and is defined as TIM_TIM2_ETR_GPIO and TIM_TIM5_ETR_GPIO in ST's
 * stm32h7xx_hal_tim_ex.h. What is still unknown for that route is which pin
 * carries TIM2_ETR or TIM5_ETR on this package, because no example in the pack
 * configures either. So the narrower counter is the one with a complete set of
 * facts behind it, and this instrument uses that rather than the wider one with
 * a gap in it.
 *
 * AN LPTIM SAMPLES ITS INPUT RATHER THAN COUNTING EDGES ASYNCHRONOUSLY, which is
 * the constraint that shapes everything else here and which ST states outright
 * in that example's readme: "the external input is sampled with LSI clock. In
 * order not to miss any event, the frequency of the changes on the external
 * Input1 signal should never exceed the frequency of the internal clock provided
 * to the LPTIM1".
 *
 * So this instrument has a CEILING, and the ceiling is a configuration choice
 * rather than a property of the part. With LPTIM1 on the LSI it counts to about
 * 32 kHz; on an APB clock it counts far higher. Three things follow and all
 * three belong in the chapter:
 *
 *   - the ceiling has to be printed beside every measurement, because a signal
 *     above it does not read low, it reads wrong by however many edges were
 *     missed, and nothing in the count says so.
 *   - the kernel clock choice does NOT affect accuracy. The count is a number
 *     of edges and the gate is the crystal, so a PLL-derived kernel clock costs
 *     range and not correctness. That distinction is the whole reason the gate
 *     moved to the crystal earlier today and it is worth not confusing.
 *   - 1 kHz, which is what P06 asks this instrument to check, is three decades
 *     below even the LSI ceiling. The method document's 0.1 per cent tolerance
 *     is met by a one second gate with room to spare.
 *
 * CONFIRMED ON THE BOARD ON MONDAY 5 OCTOBER 2026, which is what p01-pll280's
 * freqcount section exists for. freqcount_init returned 0 both before and after
 * the clock was raised, which means LPTIM1 accepted the configuration and read
 * back an autoreload of 0xFFFF. That is the test of LPTIM1_BASE, which this
 * repository DERIVED at 0x40002400 from USART3's absolute address rather than
 * read anywhere, and a wrong base would have failed exactly there. The sampling
 * ceiling read 64000000 Hz on the reset clock and 140000000 Hz at the 280 MHz
 * setting, tracking the clock tree as it should.
 *
 * WHAT THAT RUN DID NOT SHOW, because nothing was connected to PD12: the
 * counting path. Both measurements returned 0 millihertz, which is the correct
 * frequency of an idle line held high by its pull-up and is also what a refusal
 * returns.
 *
 * ---------------------------------------------------------------------------
 * THE EXPERIMENT THAT WOULD SHOW IT, designed Monday 5 October 2026 and not yet
 * built. It is written out here because the design took three wrong turns and
 * the reasons are worth more than the conclusion.
 *
 * WRONG TURN ONE: wire LPTIM1_OUT to LPTIM1_IN1. ST's LPTIM_PWMExternalClock for
 * this board puts LPTIM1_OUT on PD13 and LPTIM1_IN1 on PD12, adjacent pins, so
 * one wire looks like a self test. It is not. That example's name says why: the
 * counter is CLOCKED by IN1 and the compare generates the PWM on OUT, so the
 * output frequency is derived from the input. Wiring one to the other makes a
 * feedback loop, and counting a signal generated from the counter being tested
 * proves nothing at all.
 *
 * WRONG TURN TWO: use ST's LL PWM example, which is the obvious source for a
 * known frequency. Examples_LL/TIM/TIM_PWMOutput drives TIM3 channel 3 on PB0 at
 * alternate function 2. PB0 is LD1, the green LED, which this header has carried
 * as a settled board fact since Friday 2 October 2026 and which p01-pll280
 * blinks as part of its own evidence. Taking that pin would fight the one
 * indicator the image uses.
 *
 * WHAT IS LEFT, and it collides with nothing: TIM1 channel 3 on PE13 at
 * alternate function 1, push-pull with a pull-up, from
 * Projects/NUCLEO-H7A3ZI-Q/Examples/TIM/TIM_DMA in STM32Cube_FW_H7_V1.13.0. PE13
 * is not LD1 on PB0, not LD2 on PE1, not LD3 on PB14, not the button on PC13,
 * not the console on PD8 and PD9, and not the counting input on PD12. The
 * arrangement is therefore one wire from PE13 to PD12 and a PWM frequency
 * chosen well under the 16777216 Hz usable maximum.
 *
 * WHY THIS IS BETTER THAN A SELF TEST, which is the part worth keeping. A
 * general purpose timer's output is derived from its APB clock and therefore
 * from the PLL. ST's own LL example makes that explicit, computing its prescaler
 * from SystemCoreClock. So counting that output against the crystal gate
 * compares the PLL to the crystal through a path that shares NOTHING with
 * c/instr/lseref.c: lseref counts core cycles inside the part with DWT_CYCCNT,
 * while this counts edges arriving on a pin from outside the counter. Two
 * instruments, one quantity, no common component but the crystal itself.
 *
 * AND IT CAN FAIL INFORMATIVELY, which a self test cannot. If the two agree, the
 * crystal gate and the cycle counter are confirmed against each other and the
 * four core clock readings of Sunday 4 October 2026 and Monday 5 October 2026
 * gain a second witness. If they disagree, one of them is wrong and the SIZE of
 * the disagreement says which kind: a ratio near a small integer points at a
 * prescaler or a divider misread, a few hundred parts per million points at the
 * crystal, and a disagreement that moves between runs points back at the
 * debugger's 8 MHz, which is already known to move by a part in a thousand.
 *
 * WHAT STILL BLOCKS IT is the connector and nothing else. Both pins have to
 * reach the Zio header for a wire to join them, and the pack carries no Zio map.
 * That is UM2407 for the MB1363, now needed for PE13 as well as PD12. Every
 * register fact for both ends is in hand.
 * --------------------------------------------------------------------------- */

/* ONE THING IS STILL A BOARD FACT NOBODY HAS SOURCED: whether PD12 reaches the
 * Zio header. ST's example drives the pin without saying where it appears on the
 * connector, and the pack carries no Zio map. That needs UM2407 for the MB1363,
 * and until it is read this instrument can be written and cannot be wired. The
 * refusal in freqcount_init is for exactly that and not for anything in the
 * list above.
 */
#ifndef FREQCOUNT_H
#define FREQCOUNT_H

#include <stdint.h>

/* Configure LPTIM1 to count edges on PD12, and start the crystal the gate needs.
 *
 * Returns 0 when the counter is running. Negative when something it wrote did
 * not read back, when the autoreload write was never acknowledged, or when the
 * 32.768 kHz crystal did not start within three seconds. Each of those is a
 * refusal rather than a degraded mode: a counter that is not counting the signal
 * reports a plausible frequency, which is the failure this whole volume is
 * about.
 *
 * It does NOT fail because PD12's route to the Zio header is unestablished. That
 * is a wiring question and this is a register one, and conflating them would
 * make the code refuse for a reason the code cannot check. */
int freqcount_init(void);

/* The highest input frequency the SAMPLING clock can follow, in hertz, or 0 when
 * the clock tree could not be decoded.
 *
 * This is the LPTIM1 kernel clock, which freqcount.c selects as the APB1 clock,
 * so it is 64 MHz on the reset clock and 140 MHz at the 280 MHz setting and
 * board_pclk1_hz() is where it comes from. Above it, edges are missed rather
 * than counted, and a missed edge does not announce itself.
 *
 * THE USABLE LIMIT IS LOWER THAN THIS AND freqcount.c DERIVES IT: the counter is
 * 16 bits and its wrap flag is polled once per crystal tick, so at most one wrap
 * can be accounted for per tick, which is 65536 edges in 1/256 of a second, or
 * 16777216 Hz. A measurement at or above either limit returns 0. */
uint32_t freqcount_ceiling_hz(void);

/* Blocking measurement over a gate of the given length. Returns the frequency
 * in millihertz so the caller does not need floating point, or 0 on failure.
 * Its own resolution is reported by freqcount_resolution_mhz so a caller can
 * never quote a figure finer than the instrument supports. */
uint64_t freqcount_measure_mhz(uint32_t gate_ms);

uint64_t freqcount_resolution_mhz(uint32_t gate_ms);

#endif /* FREQCOUNT_H */
