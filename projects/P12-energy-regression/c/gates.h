/* P12's two gates in C: a measurement, a committed expectation, and a verdict
 * that must be able to fail.
 *
 * The same rules, in the same order, as
 * projects/P12-energy-regression/python/gates.py. The order is not a matter of
 * style: the charge gate's four refusals are checked in a fixed sequence, and an
 * implementation that named a different first refusal for the same ledger would
 * be a real disagreement. PROTOCOL.md in this project says so and the parity
 * test compares it.
 *
 * WHAT THIS FILE DOES NOT DO, deliberately. It does not read JSON. The gate that
 * actually runs reads a ledger and a baseline from files, and the Python does
 * that; a JSON parser written three more times would become the thing under test
 * instead of the gate. This file takes the case already parsed, which is what
 * PROTOCOL.md defines, and writes the canonical answer the four implementations
 * are compared on.
 *
 * It allocates nothing and it has no global state, so the same code compiles for
 * the target. Whether it would ever run there is a separate question: the gate
 * runs on a laptop or a Raspberry Pi beside the board, not on the board, and the
 * project README says why.
 */
#ifndef P12_GATES_H
#define P12_GATES_H

#include <stdbool.h>
#include <stddef.h>

/* The chapter's figure: a five percent charge regression must turn a build red. */
#define GATE_CHARGE_TOLERANCE_DEFAULT 0.05

/* Long enough for every target and phase name in this repository, and checked
 * rather than assumed: a longer name is refused rather than truncated, because a
 * truncated name could collide with another and the gate would compare the wrong
 * budget against the wrong binary. */
#define GATE_NAME_MAX 48

/* More than the twenty targets and the five phases this repository has, and the
 * functions refuse rather than overrun. */
#define GATE_ITEMS_MAX 64

/* One target's reported or budgeted sizes. A field that the build did not report
 * at all is a different case from one it reported as zero, which is what the two
 * flags carry. */
typedef struct {
    char name[GATE_NAME_MAX];
    long flash;
    long static_ram;
    bool has_flash;
    bool has_static_ram;
} gate_size_t;

/* One phase's charge in microcoulombs. */
typedef struct {
    char name[GATE_NAME_MAX];
    double microcoulombs;
} gate_phase_t;

/* Why a call was refused before it produced an answer at all. These are faults
 * in the call rather than verdicts on the measurement, and they are distinct
 * from the gate refusals named in PROTOCOL.md, which ARE verdicts and appear in
 * the answer. */
typedef enum {
    GATE_OK = 0,
    GATE_ERR_ARGS = -1,       /* a null pointer, or a negative tolerance */
    GATE_ERR_TOO_MANY = -2,   /* more items than GATE_ITEMS_MAX */
    GATE_ERR_NAME = -3,       /* a name that is not terminated within GATE_NAME_MAX */
    GATE_ERR_SPACE = -4       /* the answer does not fit the caller's buffer */
} gate_status_t;

/* The name of a status, for a message a reader can act on. */
const char *gate_status_name(gate_status_t status);

/* The size gate. Writes the canonical answer of PROTOCOL.md into `out`.
 *
 * Neither array need be sorted: the gate sorts by name, as the Python's
 * `sorted()` over its dictionary keys does, so that two implementations handed
 * the same case in different orders still agree. */
gate_status_t gates_size_answer(const gate_size_t *measured, size_t measured_n,
                                const gate_size_t *budgets, size_t budgets_n,
                                char *out, size_t cap);

/* The charge gate. `has_total` false is a ledger that carries no total at all,
 * which is refused rather than treated as zero. */
gate_status_t gates_charge_answer(const char *build,
                                  const gate_phase_t *phases, size_t phases_n,
                                  bool has_total, double total_uc,
                                  const gate_phase_t *baseline, size_t baseline_n,
                                  double tolerance,
                                  char *out, size_t cap);

#endif /* P12_GATES_H */
