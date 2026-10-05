/* c/instr/freqmath.h: the frequency counter's arithmetic, callable on any host.
 *
 * WHY THIS IS A SEPARATE FILE, and the reason is the same one c/clock/clocktree.h
 * gives at length. c/instr/freqcount.c dereferences LPTIM1 and the GPIO port, so
 * nothing on a host can call it, so the only test its arithmetic could ever have
 * is to flash the board and believe the number. That is a poor way to check a
 * divide.
 *
 * The register work stays in freqcount.c. Everything that is only arithmetic
 * moves here, takes its inputs as arguments, and gets driven from a host against
 * a table of cases in python/tests/test_freqmath.py.
 *
 * WHAT IS ACTUALLY AT RISK IN THIS ARITHMETIC, which is why it is worth the
 * extra file. Four things, and each has at least one case in that table:
 *
 *   - MULTIPLY BEFORE DIVIDE. The frequency is edges times the tick rate over
 *     the number of ticks, in millihertz. At 1 kHz over a one second gate the
 *     edge count is 1000 and dividing before multiplying throws away three
 *     digits. The wrong order still produces a plausible number.
 *   - THE 64-BIT INTERMEDIATE. 65535 edges times 256 ticks per second times a
 *     thousand is about 1.7e10, which leaves 32 bits. A 32-bit intermediate
 *     wraps and reports a frequency that is not merely wrong but unrelated.
 *   - TWO REFUSALS WITH DIFFERENT THRESHOLDS. The sampling ceiling is the
 *     kernel clock. The usable maximum is lower and is the counter's width
 *     times the tick rate, because the wrap flag is only polled once per tick.
 *     Getting either comparison the wrong side of its boundary turns a refusal
 *     into a wrong answer, which is the one outcome this instrument must not
 *     have.
 *   - THE COUNTER DIFFERENCE ACROSS A WRAP. The composed counter is the wrap
 *     count in the high bits and LPTIM1's 16-bit value in the low ones, and the
 *     subtraction has to be modular so that a gate spanning a composed wrap
 *     still gives the right difference.
 *
 * NOTHING HERE READS A REGISTER OR A CLOCK. Every function is a pure function of
 * its arguments, which is what makes the table in the test an oracle rather than
 * a recording.
 */
#ifndef FREQMATH_H
#define FREQMATH_H

#include <stdint.h>

/* The composed counter: a wrap count above LPTIM1's 16 bits.
 *
 * Returned as uint32_t rather than uint64_t deliberately, so that the
 * subtraction below is modular at 32 bits and a gate that spans a composed wrap
 * still gives the right difference. That costs nothing: 2^32 edges at the usable
 * maximum of 16777216 Hz is over four minutes, and no gate in this volume is
 * longer than a second. */
uint32_t freqmath_compose(uint32_t wraps, uint32_t counter);

/* Edges between two composed readings, modular at 32 bits.
 *
 * A function rather than a subtraction at the call site because the modular part
 * is the whole point and an inline `after - before` invites somebody to add a
 * comparison that breaks it. */
uint32_t freqmath_edges(uint32_t before, uint32_t after);

/* The highest input this instrument can account for, in hertz.
 *
 * (arr + 1) times the tick rate: the counter holds arr + 1 distinct values and
 * its wrap flag is read once per tick, so one full counter per tick is the most
 * that can be followed. With arr 0xFFFF and 256 Hz that is 16777216 Hz exactly.
 * 0 when either argument is 0, because a limit nobody can state is not a limit. */
uint32_t freqmath_usable_max_hz(uint32_t arr, uint32_t tick_hz);

/* The frequency, in MILLIHERTZ, or 0 when it will not be stood behind.
 *
 * edges over (ticks / tick_hz) seconds, times a thousand. Refuses when:
 *   - tick_hz or ticks is 0, so the gate is not a known length
 *   - ceiling_hz is non-zero and the result reaches it, so edges were sampled
 *     too slowly to be trusted
 *   - the result reaches freqmath_usable_max_hz, so a wrap may not have been seen
 *
 * A GENUINELY IDLE INPUT ALSO RETURNS 0, and that ambiguity is real rather than
 * an oversight: zero edges in the gate is zero hertz, and this function has no
 * way to tell that from a refusal. The caller knows whether a signal is
 * connected and this function does not, so the distinction belongs there. It is
 * stated here because a reader who assumes 0 means failure will mis-read an
 * idle line, and one who assumes it means zero hertz will mis-read a refusal. */
uint64_t freqmath_mhz(uint32_t edges, uint32_t tick_hz, uint32_t ticks,
                      uint32_t ceiling_hz, uint32_t arr);

/* How many ticks a requested gate in milliseconds comes to, never less than 1.
 *
 * The tick rate has to be supplied because this conversion happens before the
 * hardware reports its actual rate. freqcount.c passes the rate it expects and
 * then does the arithmetic above with the rate that came back, so a surprise in
 * RTC_PRER changes the gate's length and not the frequency. */
uint32_t freqmath_ticks_for_ms(uint32_t gate_ms, uint32_t tick_hz);

/* One edge in the gate, in millihertz: the instrument's own resolution. A one
 * second gate resolves one hertz, which is 1000 millihertz. 0 for a zero gate. */
uint64_t freqmath_resolution_mhz(uint32_t gate_ms);

#endif /* FREQMATH_H */
