/* atparse.c: the classifier, the deadline test and the backoff.
 *
 * Every narrowing is an explicit cast and every intermediate is unsigned, since
 * the host build compiles this with -Wconversion and -Wsign-conversion as errors.
 */
#include "atparse.h"

#include <stddef.h>
#include <string.h>

/* The tables of chapter 11's step 5, visible here because the module's manual
 * adds to them and a table is where an addition goes. */
static const char *const FINAL_OK[] = { "OK", "CONNECT", NULL };
static const char *const FINAL_BAD[] = {
    "ERROR", "+CME ERROR:", "+CMS ERROR:", "NO CARRIER", "ABORTED", NULL
};

/* A prefix match and not an equality test, which matters for exactly one row in
 * each table: "+CME ERROR:" is always followed by a number, and "CONNECT" is
 * followed by a baud rate on some modules and by nothing on others. Matching the
 * whole line would classify "+CME ERROR: 10" as intermediate and attach it to the
 * command as data. */
static bool starts_with(const char *line, const char *prefix)
{
    const size_t n = strlen(prefix);
    return strncmp(line, prefix, n) == 0;
}

static bool match_any(const char *line, const char *const *table)
{
    for (size_t i = 0u; table[i] != NULL; i++) {
        if (starts_with(line, table[i])) {
            return true;
        }
    }
    return false;
}

at_class_t at_classify(const char *line, const char *const *urcs)
{
    if (line == NULL || line[0] == '\0') {
        return AT_CLASS_EMPTY;
    }
    if (match_any(line, FINAL_OK)) {
        return AT_CLASS_FINAL_OK;
    }
    if (match_any(line, FINAL_BAD)) {
        return AT_CLASS_FINAL_BAD;
    }
    if (urcs != NULL && match_any(line, urcs)) {
        return AT_CLASS_URC;
    }
    return AT_CLASS_INTERMEDIATE;
}

bool at_due(uint32_t now_ms, uint32_t deadline_ms)
{
    /* The subtraction is unsigned, so it wraps rather than being undefined, and
     * only the result is read as signed. Writing the subtraction itself on
     * signed values would be undefined behaviour on overflow, which is the
     * version of this that compiles, passes and then surprises somebody. */
    const uint32_t diff = now_ms - deadline_ms;
    return (int32_t) diff >= 0;
}

uint32_t at_backoff_ms(uint8_t attempt, uint32_t base_ms, uint32_t cap_ms,
                       uint32_t rand_draw)
{
    if (base_ms == 0u || cap_ms < base_ms) {
        return AT_BACKOFF_REFUSED;
    }
    /* Above half the range the doubling below can overflow, and an overflowed
     * delay comes back as a plausible small number rather than as an error. The
     * header says so; this is where it is refused. */
    if (cap_ms > (0xFFFFFFFFu / 2u)) {
        return AT_BACKOFF_REFUSED;
    }

    uint32_t d = base_ms;
    for (uint8_t i = 0u; i < attempt && d < cap_ms; i++) {
        d <<= 1;
    }
    if (d > cap_ms) {
        d = cap_ms;
    }

    /* The scatter of step 8, kept exactly as the chapter writes it: take an
     * eighth off, then add a draw from a quarter. The window is a quarter wide
     * and sits an eighth below, so the result runs from seven eighths of the
     * interval to one and an eighth of it, which is why it can pass the cap. */
    const uint32_t span = (d >> 2) != 0u ? (d >> 2) : 1u;
    return d - (d >> 3) + (rand_draw % span);
}
