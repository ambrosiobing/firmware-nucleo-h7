#!/usr/bin/env python3
"""The three gates, in increasing order of what they cost to run.

    python projects/P12-energy-regression/python/gates.py size   build-host/sizes.json
    python projects/P12-energy-regression/python/gates.py charge captures/ledger.json

GATE 1, the host unit tests, costs seconds on a runner with no board attached and
catches most ordinary defects. It is not in this file because it is already the
rest of this repository: 71 checks across P02, P05, P08 and P09.

GATE 2, the size gate, reads the sizes a build reported and fails when flash or
static memory has grown past a stated limit. It costs nothing and catches the class
of change that quietly consumes the margin, which is the class nobody notices until
the day a build does not fit.

GATE 3, the charge gate, compares a measured energy ledger against a committed
baseline and fails on a regression. It is the only one that needs hardware, which is
why it runs last and on a different machine.

WHY BOTH GATES LIVE IN ONE FILE. They are the same shape: a measurement, a
committed expectation, a tolerance, and a verdict that must be able to fail. Writing
them twice would invite them to drift apart in the one respect that matters, which
is what they do when the measurement is missing rather than merely large.

THE RULE THAT MAKES A GATE A GATE. A missing measurement is a failure, not a pass.
A gate that skips when it cannot find its input is not a gate: it is a gate-shaped
hole that reports green on the day the build stops producing the thing it checks.
Both functions below refuse rather than skip, and the tests pin that.

Proven on synthetic input by python/tests/test_gates.py, including the chapter's own
acceptance criterion: a deliberate five percent charge regression turns the build
red with nobody at the bench.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
BUDGETS = ROOT / "projects" / "P12-energy-regression" / "budgets.json"
BASELINE = ROOT / "projects" / "P12-energy-regression" / "baseline.json"

# The chapter's figure: a five percent charge regression must turn the build red.
CHARGE_TOLERANCE_FRACTION = 0.05


class GateFailure(Exception):
    """Raised with a message a reader can act on, not merely a status."""


def _load(path: Path, what: str) -> dict:
    if not path.exists():
        raise GateFailure(
            "{} is missing at {}.\n"
            "A gate with no expectation to compare against is not a gate. Commit "
            "the file, or\nstate explicitly that this target has no budget yet."
            .format(what, path))
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise GateFailure("{} at {} is not valid JSON: {}".format(what, path, exc))


# ------------------------------------------------------------------ gate 2, size

def size_verdict(measured: dict, budgets: dict) -> dict:
    """The size gate's decision as data rather than as prose.

    This exists so the gate can be compared with the C, C++ and Rust
    implementations of the same rules. `check_sizes` renders its English from
    this, so there is one set of rules and not two and the wording cannot drift
    away from the verdict it describes.

    Returns {"refused": tuple or None, "failures": [tuple], "report": [tuple]}.
    `refused` is the one failure that stops the gate before it looks at anything
    else, because a gate with no input at all has nothing to report.
    """
    if not measured:
        return {"refused": ("no_sizes",), "failures": [], "report": []}

    failures, report = [], []
    for target in sorted(measured):
        got = measured[target]
        if target not in budgets:
            failures.append(("no_budget", target))
            continue
        want = budgets[target]
        for field in ("flash", "static_ram"):
            if field not in got:
                failures.append(("no_field", target, field))
                continue
            limit = want.get(field)
            if limit is None:
                failures.append(("no_field_budget", target, field))
                continue
            used = got[field]
            report.append((target, field, used, limit,
                           (100.0 * used / limit) if limit else 0.0, limit - used))
            if used > limit:
                failures.append(("over", target, field, used, limit, used - limit))
    return {"refused": None, "failures": failures, "report": report}


def check_sizes(measured: dict, budgets: dict | None = None) -> list[str]:
    """Compare reported sizes against committed budgets. Returns the report lines.

    `measured` maps a target name to {"flash": int, "static_ram": int}.
    Raises GateFailure on any overrun, or on a target with no budget.

    A target that appears in the build and not in the budgets is a failure rather
    than something to ignore: a new target with no budget is how a project acquires
    a binary nobody is watching the size of.
    """
    if budgets is None:
        budgets = _load(BUDGETS, "the size budgets")

    verdict = size_verdict(measured, budgets)
    if verdict["refused"] is not None:
        raise GateFailure(
            "no sizes were reported. The build did not produce the sizes this gate "
            "reads,\nwhich is a build failure wearing a passing gate's clothes.")

    lines = [
        "  {:<22} {:<11} {:>7} of {:>7}  {:>5.1f}%  margin {:>7}".format(
            target, field, used, limit, pct, margin)
        for target, field, used, limit, pct, margin in verdict["report"]
    ]

    failures = []
    for item in verdict["failures"]:
        kind = item[0]
        if kind == "no_budget":
            failures.append(
                "{}: no committed budget. Add one to {} rather than letting a "
                "binary go unwatched.".format(item[1], BUDGETS.name))
        elif kind == "no_field":
            failures.append("{}: the build reported no {}".format(item[1], item[2]))
        elif kind == "no_field_budget":
            failures.append("{}: no {} budget committed".format(item[1], item[2]))
        elif kind == "over":
            failures.append(
                "{} {}: {} bytes over the committed {} by {}".format(
                    item[1], item[2], item[3], item[4], item[5]))

    if failures:
        raise GateFailure("the size gate failed:\n  " + "\n  ".join(failures))
    return lines


# ---------------------------------------------------------------- gate 3, charge

def charge_verdict(measured: dict, baseline: dict,
                   tolerance: float = CHARGE_TOLERANCE_FRACTION) -> dict:
    """The charge gate's decision as data rather than as prose.

    The same arrangement as `size_verdict`, and for the same reason: the C, C++
    and Rust implementations of these rules are compared against this one, and
    `check_charge` renders its English from this so the wording cannot drift away
    from the verdict it describes.

    **The order of the four refusals is part of the rules and not an accident of
    how the code reads.** A ledger with no build identity is refused before its
    phases are examined, because a capture that cannot be tied to a firmware is
    not evidence whatever its numbers say. The sum check comes before any phase
    is compared, because a run whose parts do not reconcile has lost or double
    counted a phase and no verdict on it is worth anything. An implementation
    that reported a different first refusal for the same ledger would be a real
    disagreement, so the parity test compares which refusal came first.
    """
    if not measured.get("build"):
        return {"refused": ("no_build",), "failures": [], "report": []}

    phases = measured.get("phases")
    want_phases = baseline.get("phases")
    if not phases or not want_phases:
        return {"refused": ("no_phases",), "failures": [], "report": []}

    total = measured.get("total_uc")
    if total is None:
        return {"refused": ("no_total",), "failures": [], "report": []}
    # Summed in name order and not in the order the ledger happened to list
    # them. Addition is not associative in floating point, and this sum appears
    # in the answer when it does not reconcile, so its order is part of the rules
    # rather than an accident: the C sorts an array of pointers before summing,
    # and the C++ and the Rust get the order from their containers. Relying on a
    # comparison tolerance to absorb the difference would have worked here and
    # would have been the wrong reason for it to work. P06 taught this one.
    summed = sum(phases[name] for name in sorted(phases))
    if abs(summed - total) > max(1.0, 0.05 * total):
        return {"refused": ("sum_mismatch", summed, total), "failures": [],
                "report": []}

    failures, report = [], []
    for phase in sorted(want_phases):
        if phase not in phases:
            failures.append(("missing_phase", phase))
            continue
        want = want_phases[phase]
        got = phases[phase]
        change = (100.0 * (got - want) / want) if want else 0.0
        report.append((phase, got, want, change))
        if got > want * (1.0 + tolerance):
            failures.append(("over", phase, got, want, change))

    for phase in sorted(phases):
        if phase not in want_phases:
            failures.append(("not_in_baseline", phase))

    return {"refused": None, "failures": failures, "report": report}


def check_charge(measured: dict, baseline: dict | None = None,
                 tolerance: float = CHARGE_TOLERANCE_FRACTION) -> list[str]:
    """Compare a measured energy ledger against the committed baseline.

    `measured` maps a phase name to its charge in microcoulombs, and must also
    carry a "build" identity, because a capture with no build identity is not
    evidence.

    Raises GateFailure when any phase exceeds its baseline by more than the
    tolerance, when a phase is missing, or when the total does not reconcile with
    the sum of the phases.
    """
    if baseline is None:
        baseline = _load(BASELINE, "the charge baseline")

    verdict = charge_verdict(measured, baseline, tolerance)
    refused = verdict["refused"]
    if refused is not None:
        if refused[0] == "no_build":
            raise GateFailure(
                "this ledger records no build identity, so it is not evidence and "
                "cannot gate\nanything. scan records git describe; a capture without "
                "it cannot be tied to the\nfirmware that produced it.")
        if refused[0] == "no_phases":
            raise GateFailure("the ledger or the baseline carries no phases")
        if refused[0] == "no_total":
            raise GateFailure("the ledger carries no total_uc to reconcile against")
        raise GateFailure(
            "the phases sum to {:.1f} uC and the ledger reports a total of {:.1f}. "
            "A phase is\nmissing or double counted, and no verdict on this run is "
            "worth anything.".format(refused[1], refused[2]))

    lines = [
        "  {:<12} {:>9.2f} uC   baseline {:>9.2f}   {:+6.1f}%".format(
            phase, got, want, change)
        for phase, got, want, change in verdict["report"]
    ]

    failures = []
    for item in verdict["failures"]:
        kind = item[0]
        if kind == "missing_phase":
            failures.append(
                "{}: the ledger is missing this phase. A missing measurement is a "
                "failure, not a pass.".format(item[1]))
        elif kind == "over":
            failures.append(
                "{}: {:.2f} uC against a baseline of {:.2f}, which is {:+.1f}% and "
                "over the {:.0f}% tolerance".format(
                    item[1], item[2], item[3], item[4], tolerance * 100))
        elif kind == "not_in_baseline":
            failures.append(
                "{}: measured but not in the baseline. Add it, or the gate is "
                "watching less than the run does.".format(item[1]))

    if failures:
        raise GateFailure("the charge gate failed:\n  " + "\n  ".join(failures))
    return lines


# ------------------------------------------------------------------------- main

def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[1] not in ("size", "charge"):
        print(__doc__.splitlines()[0])
        print("usage: python projects/P12-energy-regression/python/gates.py size|charge <file.json>")
        return 2

    try:
        payload = _load(Path(argv[2]), "the measurement")
        if argv[1] == "size":
            print("size gate")
            for line in check_sizes(payload):
                print(line)
        else:
            print("charge gate, tolerance {:.0f}%".format(CHARGE_TOLERANCE_FRACTION * 100))
            print("  build {}".format(payload.get("build")))
            for line in check_charge(payload):
                print(line)
    except GateFailure as exc:
        print("\n{}".format(exc))
        return 1

    print("\npassed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
