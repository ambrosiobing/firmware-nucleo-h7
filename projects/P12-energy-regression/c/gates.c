/* P12's two gates in C. See gates.h for what this file is and is not. */
#include "gates.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Every double in the answer is written with seventeen significant digits, which
 * round-trips a double exactly, and PROTOCOL.md says why: a gate's own two
 * decimal places are a decimal conversion, and four languages need not agree on
 * the tie case. The comparison is of the arithmetic, not of four printers. */
#define G17 "%.17g"

const char *gate_status_name(gate_status_t status)
{
    switch (status) {
    case GATE_OK:            return "ok";
    case GATE_ERR_ARGS:      return "args";
    case GATE_ERR_TOO_MANY:  return "too_many";
    case GATE_ERR_NAME:      return "name";
    case GATE_ERR_SPACE:     return "space";
    default:                 return "unknown";
    }
}

/* ------------------------------------------------------------- an appender
 *
 * One cursor over the caller's buffer, refusing rather than truncating. A
 * truncated answer would be compared against three full ones and read as a
 * disagreement about the gate, which is the most misleading thing this file
 * could produce. */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
    bool   full;
} sink_t;

static void sink_init(sink_t *s, char *buf, size_t cap)
{
    s->buf = buf;
    s->cap = cap;
    s->len = 0;
    s->full = false;
    if (cap > 0) {
        buf[0] = '\0';
    } else {
        s->full = true;
    }
}

static void sink_add(sink_t *s, const char *fmt, ...)
{
    va_list ap;
    int wrote;

    if (s->full) {
        return;
    }
    va_start(ap, fmt);
    wrote = vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);

    if (wrote < 0 || (size_t) wrote >= s->cap - s->len) {
        s->full = true;
        return;
    }
    s->len += (size_t) wrote;
}

/* ------------------------------------------------------------ name handling */

/* A name must be terminated inside the array. A name that is not is refused
 * rather than truncated, because two truncated names could collide and the gate
 * would then compare one target's budget against another's binary. */
static bool name_ok(const char *name)
{
    size_t i;

    for (i = 0; i < GATE_NAME_MAX; i++) {
        if (name[i] == '\0') {
            return true;
        }
    }
    return false;
}

/* The order of the items, by name. Python sorts its dictionary keys by code
 * point and this compares bytes, which agree for the ASCII names this
 * repository uses. PROTOCOL.md records that as a stated limit rather than an
 * assumption. */
static void order_sizes(const gate_size_t *in, size_t n, const gate_size_t **out)
{
    size_t i, j;

    for (i = 0; i < n; i++) {
        out[i] = &in[i];
    }
    for (i = 1; i < n; i++) {
        const gate_size_t *key = out[i];
        j = i;
        while (j > 0 && strcmp(out[j - 1]->name, key->name) > 0) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }
}

static void order_phases(const gate_phase_t *in, size_t n, const gate_phase_t **out)
{
    size_t i, j;

    for (i = 0; i < n; i++) {
        out[i] = &in[i];
    }
    for (i = 1; i < n; i++) {
        const gate_phase_t *key = out[i];
        j = i;
        while (j > 0 && strcmp(out[j - 1]->name, key->name) > 0) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }
}

static const gate_size_t *find_size(const gate_size_t *items, size_t n,
                                    const char *name)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (strcmp(items[i].name, name) == 0) {
            return &items[i];
        }
    }
    return NULL;
}

static const gate_phase_t *find_phase(const gate_phase_t *items, size_t n,
                                      const char *name)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (strcmp(items[i].name, name) == 0) {
            return &items[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------ the size gate */

gate_status_t gates_size_answer(const gate_size_t *measured, size_t measured_n,
                                const gate_size_t *budgets, size_t budgets_n,
                                char *out, size_t cap)
{
    const gate_size_t *order[GATE_ITEMS_MAX];
    sink_t failures, report;
    char fbuf[2048], rbuf[2048];
    size_t i, k;
    bool any_failure = false;

    if (out == NULL || cap == 0) {
        return GATE_ERR_ARGS;
    }
    if ((measured == NULL && measured_n > 0) || (budgets == NULL && budgets_n > 0)) {
        return GATE_ERR_ARGS;
    }
    if (measured_n > GATE_ITEMS_MAX || budgets_n > GATE_ITEMS_MAX) {
        return GATE_ERR_TOO_MANY;
    }
    for (i = 0; i < measured_n; i++) {
        if (!name_ok(measured[i].name)) {
            return GATE_ERR_NAME;
        }
    }
    for (i = 0; i < budgets_n; i++) {
        if (!name_ok(budgets[i].name)) {
            return GATE_ERR_NAME;
        }
    }

    /* A gate with no input at all has nothing to report, and reports that rather
     * than passing. A build that produced no sizes is a build failure wearing a
     * passing gate's clothes. */
    if (measured_n == 0) {
        sink_t answer;
        sink_init(&answer, out, cap);
        sink_add(&answer, "FAIL no_sizes - -");
        return answer.full ? GATE_ERR_SPACE : GATE_OK;
    }

    order_sizes(measured, measured_n, order);
    sink_init(&failures, fbuf, sizeof fbuf);
    sink_init(&report, rbuf, sizeof rbuf);

    for (k = 0; k < measured_n; k++) {
        const gate_size_t *got = order[k];
        const gate_size_t *want = find_size(budgets, budgets_n, got->name);
        int field;

        if (want == NULL) {
            sink_add(&failures, "%sno_budget:%s", any_failure ? "," : "", got->name);
            any_failure = true;
            continue;
        }
        /* flash first and then static_ram, which is the order the Python's
         * tuple states and therefore the order of the failures a reader sees. */
        for (field = 0; field < 2; field++) {
            const char *fname = (field == 0) ? "flash" : "static_ram";
            bool reported = (field == 0) ? got->has_flash : got->has_static_ram;
            bool budgeted = (field == 0) ? want->has_flash : want->has_static_ram;
            long used = (field == 0) ? got->flash : got->static_ram;
            long limit = (field == 0) ? want->flash : want->static_ram;
            double pct;

            if (!reported) {
                sink_add(&failures, "%sno_field:%s:%s",
                         any_failure ? "," : "", got->name, fname);
                any_failure = true;
                continue;
            }
            if (!budgeted) {
                sink_add(&failures, "%sno_field_budget:%s:%s",
                         any_failure ? "," : "", got->name, fname);
                any_failure = true;
                continue;
            }
            pct = (limit != 0) ? (100.0 * (double) used / (double) limit) : 0.0;
            sink_add(&report, "%s%s:%s:%ld:%ld:" G17 ":%ld",
                     (report.len > 0) ? "," : "", got->name, fname,
                     used, limit, pct, limit - used);
            if (used > limit) {
                sink_add(&failures, "%sover:%s:%s:%ld:%ld:%ld",
                         any_failure ? "," : "", got->name, fname,
                         used, limit, used - limit);
                any_failure = true;
            }
        }
    }

    if (failures.full || report.full) {
        return GATE_ERR_SPACE;
    }

    {
        sink_t answer;
        sink_init(&answer, out, cap);
        sink_add(&answer, "%s - %s %s",
                 any_failure ? "FAIL" : "PASS",
                 (failures.len > 0) ? fbuf : "-",
                 (report.len > 0) ? rbuf : "-");
        return answer.full ? GATE_ERR_SPACE : GATE_OK;
    }
}

/* ---------------------------------------------------------- the charge gate */

gate_status_t gates_charge_answer(const char *build,
                                  const gate_phase_t *phases, size_t phases_n,
                                  bool has_total, double total_uc,
                                  const gate_phase_t *baseline, size_t baseline_n,
                                  double tolerance,
                                  char *out, size_t cap)
{
    const gate_phase_t *order[GATE_ITEMS_MAX];
    sink_t failures, report, answer;
    char fbuf[2048], rbuf[2048];
    size_t i, k;
    bool any_failure = false;
    double summed = 0.0;

    if (out == NULL || cap == 0 || tolerance < 0.0) {
        return GATE_ERR_ARGS;
    }
    if ((phases == NULL && phases_n > 0) || (baseline == NULL && baseline_n > 0)) {
        return GATE_ERR_ARGS;
    }
    if (phases_n > GATE_ITEMS_MAX || baseline_n > GATE_ITEMS_MAX) {
        return GATE_ERR_TOO_MANY;
    }
    for (i = 0; i < phases_n; i++) {
        if (!name_ok(phases[i].name)) {
            return GATE_ERR_NAME;
        }
    }
    for (i = 0; i < baseline_n; i++) {
        if (!name_ok(baseline[i].name)) {
            return GATE_ERR_NAME;
        }
    }

    sink_init(&answer, out, cap);

    /* The four refusals, in the order PROTOCOL.md fixes. Build identity first:
     * a capture that cannot be tied to a firmware is not evidence whatever its
     * numbers say, so there is no point examining them. */
    if (build == NULL || build[0] == '\0') {
        sink_add(&answer, "FAIL no_build - -");
        return answer.full ? GATE_ERR_SPACE : GATE_OK;
    }
    if (phases_n == 0 || baseline_n == 0) {
        sink_add(&answer, "FAIL no_phases - -");
        return answer.full ? GATE_ERR_SPACE : GATE_OK;
    }
    if (!has_total) {
        sink_add(&answer, "FAIL no_total - -");
        return answer.full ? GATE_ERR_SPACE : GATE_OK;
    }

    /* Summed in the sorted order rather than the given order, so that two
     * implementations handed the same phases in different orders accumulate the
     * same double. Addition is not associative in floating point, and this is
     * the one place in this file where the order of a sum is visible in the
     * answer. */
    order_phases(phases, phases_n, order);
    for (k = 0; k < phases_n; k++) {
        summed += order[k]->microcoulombs;
    }

    /* A run whose parts do not reconcile has lost or double counted a phase, and
     * no verdict on it is worth anything. This comes before any phase is
     * compared for exactly that reason. */
    {
        double slack = 0.05 * total_uc;
        if (slack < 1.0) {
            slack = 1.0;
        }
        if (fabs(summed - total_uc) > slack) {
            sink_add(&answer, "FAIL sum_mismatch:" G17 ":" G17 " - -",
                     summed, total_uc);
            return answer.full ? GATE_ERR_SPACE : GATE_OK;
        }
    }

    sink_init(&failures, fbuf, sizeof fbuf);
    sink_init(&report, rbuf, sizeof rbuf);

    /* The baseline's phases in name order. A phase the baseline watches and the
     * ledger does not report is a failure and not a pass: losing a phase looks
     * like an improvement, which is the direction that flatters the work. */
    order_phases(baseline, baseline_n, order);
    for (k = 0; k < baseline_n; k++) {
        const gate_phase_t *want = order[k];
        const gate_phase_t *got = find_phase(phases, phases_n, want->name);
        double change;

        if (got == NULL) {
            sink_add(&failures, "%smissing_phase:%s",
                     any_failure ? "," : "", want->name);
            any_failure = true;
            continue;
        }
        change = (want->microcoulombs != 0.0)
            ? (100.0 * (got->microcoulombs - want->microcoulombs) / want->microcoulombs)
            : 0.0;
        sink_add(&report, "%s%s:" G17 ":" G17 ":" G17,
                 (report.len > 0) ? "," : "", want->name,
                 got->microcoulombs, want->microcoulombs, change);
        if (got->microcoulombs > want->microcoulombs * (1.0 + tolerance)) {
            sink_add(&failures, "%sover:%s:" G17 ":" G17 ":" G17,
                     any_failure ? "," : "", want->name,
                     got->microcoulombs, want->microcoulombs, change);
            any_failure = true;
        }
    }

    /* Then the measured phases in name order. One the baseline does not carry is
     * a failure, or the gate would be watching less than the run does. */
    order_phases(phases, phases_n, order);
    for (k = 0; k < phases_n; k++) {
        if (find_phase(baseline, baseline_n, order[k]->name) == NULL) {
            sink_add(&failures, "%snot_in_baseline:%s",
                     any_failure ? "," : "", order[k]->name);
            any_failure = true;
        }
    }

    if (failures.full || report.full) {
        return GATE_ERR_SPACE;
    }

    sink_init(&answer, out, cap);
    sink_add(&answer, "%s - %s %s",
             any_failure ? "FAIL" : "PASS",
             (failures.len > 0) ? fbuf : "-",
             (report.len > 0) ? rbuf : "-");
    return answer.full ? GATE_ERR_SPACE : GATE_OK;
}
