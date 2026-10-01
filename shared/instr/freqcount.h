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
