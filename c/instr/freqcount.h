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
 * TWO 32-BIT TIMERS EXIST ON THIS PART and chapter 6 spends one of them here.
 * Chapter 20 is careful about who owns it for that reason.
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
 * WHAT IS STILL UNCONFIRMED is therefore smaller than it was: which timer offers
 * external clock mode on a pin that reaches the Zio header, that pin's alternate
 * function number, and the trigger selection value. The timer clock for the gate
 * is no longer on the list. The pin question is a board fact and needs the
 * MB1363 manual or ST's board support package, which is the same source that
 * settled the console pins on Friday 2 October 2026.
 */
#ifndef FREQCOUNT_H
#define FREQCOUNT_H

#include <stdint.h>

/* Returns 0 on success, negative if a required value is unconfirmed. */
int freqcount_init(void);

/* Blocking measurement over a gate of the given length. Returns the frequency
 * in millihertz so the caller does not need floating point, or 0 on failure.
 * Its own resolution is reported by freqcount_resolution_mhz so a caller can
 * never quote a figure finer than the instrument supports. */
uint64_t freqcount_measure_mhz(uint32_t gate_ms);

uint64_t freqcount_resolution_mhz(uint32_t gate_ms);

#endif /* FREQCOUNT_H */
