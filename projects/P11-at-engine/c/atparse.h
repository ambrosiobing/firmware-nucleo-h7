/* atparse.h: the three pieces of chapter 11 that need no module.
 *
 * WHAT THIS IS. The AT engine's public surface is submit, poll and cancel, and
 * most of it needs a modem on a serial line. Three pieces do not, and one of them
 * cannot be tested on hardware at all:
 *
 *   the line classifier   every reply line is a final result, an unsolicited
 *                         code, or an intermediate line, and nothing else
 *   the deadline test     a signed difference, so it survives the millisecond
 *                         counter wrapping after forty-nine days
 *   the backoff           double, cap, scatter
 *
 * THE DEADLINE TEST IS THE ONE WORTH HAVING FIRST. Chapter 11's step 4 writes the
 * comparison as (int32_t)(now_ms - deadline_ms) >= 0 and says in one line why:
 * the obvious form is correct for forty-nine days and then is not. **A board
 * cannot test that without waiting forty-nine days.** A host sets the counter to
 * whatever it likes, which is the whole argument for writing this half of the
 * project before the modem arrives.
 *
 * WHAT IS NOT HERE. The engine itself, its queue, the completion callback that
 * must fire exactly once in every outcome, the unsolicited handler table and the
 * hundred injected faults. Those need the module, the byte path of chapter 4 and
 * the framing of chapter 5. This file is the arithmetic and the string matching
 * underneath them.
 */
#ifndef ATPARSE_H
#define ATPARSE_H

#include <stdbool.h>
#include <stdint.h>

/* The three classes of step 5, and nothing else. An empty line is not a class:
 * modules pad replies with blank lines, and a classifier that called one an
 * intermediate result would attach it to whatever command was in flight. The
 * line assembler skips them before classification, so a caller that passes one
 * here gets AT_CLASS_EMPTY rather than a guess. */
typedef enum {
    AT_CLASS_FINAL_OK = 0,
    AT_CLASS_FINAL_BAD = 1,
    AT_CLASS_URC = 2,
    AT_CLASS_INTERMEDIATE = 3,
    AT_CLASS_EMPTY = 4
} at_class_t;

/* Classify one line, without its line ending.
 *
 * The final result codes are chapter 11's, from the standard command set: OK and
 * CONNECT end a command well, and ERROR, +CME ERROR:, +CMS ERROR:, NO CARRIER
 * and ABORTED end it badly. **The module's own manual adds more and they belong
 * in the table rather than in the code**, which is why the tables are visible in
 * the implementation and not folded into a chain of comparisons.
 *
 * urcs is a NULL terminated array of prefixes, or NULL for none. A line matching
 * one of them is unsolicited whatever state the engine is in. Passing NULL is how
 * a caller says it has no table yet, and it is not an error: every line that
 * would have been unsolicited is then intermediate, which is the behaviour of an
 * engine whose table is empty. */
at_class_t at_classify(const char *line, const char *const *urcs);

/* Has the deadline passed, with now and the deadline both free to have wrapped.
 *
 * The comparison is a signed difference on a 32 bit counter, so it is correct as
 * long as the two values are less than about twenty-four days apart, which is
 * half the wrap period. That bound is the real contract and it is stated here
 * because the obvious reading, "correct across the wrap", is only true inside
 * it. */
bool at_due(uint32_t now_ms, uint32_t deadline_ms);

/* The backoff of step 8: double the interval, cap it, then scatter it.
 *
 * THE RANDOM DRAW IS A PARAMETER AND THE CHAPTER'S SKETCH CALLS A FUNCTION. The
 * sketch reads at_rand_small() inside, which makes the result unprovable: a test
 * can only assert a band, never a value. Taking the draw as an argument makes
 * this a pure function that a host can pin exactly, and the engine passes
 * whatever source it has. That is a deliberate difference from the sketch, for a
 * reason, rather than a transcription error.
 *
 * TWO THINGS THE SKETCH'S ARITHMETIC DOES THAT ARE WORTH KNOWING. The jitter is
 * applied after the cap, so **the returned value can exceed cap_ms**, by up to an
 * eighth of it: the cap bounds the doubling and not the result. And with a cap
 * above half the range of the type, the doubling can overflow, which is why
 * at_backoff_ms refuses that case rather than returning a plausible small number.
 *
 * Returns the delay in milliseconds, or AT_BACKOFF_REFUSED. */
#define AT_BACKOFF_REFUSED 0xFFFFFFFFu

uint32_t at_backoff_ms(uint8_t attempt, uint32_t base_ms, uint32_t cap_ms,
                       uint32_t rand_draw);

#endif /* ATPARSE_H */
