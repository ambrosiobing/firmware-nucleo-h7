#!/usr/bin/env python3
"""The three gates, in increasing order of what they cost to run.

    python python/P12/gates.py size   build-host/sizes.json
    python python/P12/gates.py charge captures/ledger.json

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

    if not measured:
        raise GateFailure(
            "no sizes were reported. The build did not produce the sizes this gate "
            "reads,\nwhich is a build failure wearing a passing gate's clothes.")

    lines, failures = [], []
    for target in sorted(measured):
        got = measured[target]
        if target not in budgets:
            failures.append(
                "{}: no committed budget. Add one to {} rather than letting a "
                "binary go unwatched.".format(target, BUDGETS.name))
            continue
        want = budgets[target]
        for field in ("flash", "static_ram"):
            if field not in got:
                failures.append("{}: the build reported no {}".format(target, field))
                continue
            limit = want.get(field)
            if limit is None:
                failures.append("{}: no {} budget committed".format(target, field))
                continue
            used = got[field]
            margin = limit - used
            pct = (100.0 * used / limit) if limit else 0.0
            lines.append("  {:<22} {:<11} {:>7} of {:>7}  {:>5.1f}%  margin {:>7}".format(
                target, field, used, limit, pct, margin))
            if used > limit:
                failures.append(
                    "{} {}: {} bytes over the committed {} by {}".format(
                        target, field, used, limit, used - limit))

    if failures:
        raise GateFailure("the size gate failed:\n  " + "\n  ".join(failures))
    return lines


# ---------------------------------------------------------------- gate 3, charge

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

    if not measured.get("build"):
        raise GateFailure(
            "this ledger records no build identity, so it is not evidence and "
            "cannot gate\nanything. scan records git describe; a capture without it "
            "cannot be tied to the\nfirmware that produced it.")

    phases = measured.get("phases")
    want_phases = baseline.get("phases")
    if not phases or not want_phases:
        raise GateFailure("the ledger or the baseline carries no phases")

    # The phases must sum to the total. Without this the gate can pass while the
    # measurement has lost a phase, which is the failure that looks most like an
    # improvement: fewer phases, less total charge, everything green.
    total = measured.get("total_uc")
    if total is None:
        raise GateFailure("the ledger carries no total_uc to reconcile against")
    summed = sum(phases.values())
    if abs(summed - total) > max(1.0, 0.05 * total):
        raise GateFailure(
            "the phases sum to {:.1f} uC and the ledger reports a total of {:.1f}. "
            "A phase is\nmissing or double counted, and no verdict on this run is "
            "worth anything.".format(summed, total))

    lines, failures = [], []
    for phase in sorted(want_phases):
        if phase not in phases:
            failures.append(
                "{}: the ledger is missing this phase. A missing measurement is a "
                "failure, not a pass.".format(phase))
            continue
        want = want_phases[phase]
        got = phases[phase]
        limit = want * (1.0 + tolerance)
        change = (100.0 * (got - want) / want) if want else 0.0
        lines.append("  {:<12} {:>9.2f} uC   baseline {:>9.2f}   {:+6.1f}%".format(
            phase, got, want, change))
        if got > limit:
            failures.append(
                "{}: {:.2f} uC against a baseline of {:.2f}, which is {:+.1f}% and "
                "over the {:.0f}% tolerance".format(
                    phase, got, want, change, tolerance * 100))

    for phase in sorted(phases):
        if phase not in want_phases:
            failures.append(
                "{}: measured but not in the baseline. Add it, or the gate is "
                "watching less than the run does.".format(phase))

    if failures:
        raise GateFailure("the charge gate failed:\n  " + "\n  ".join(failures))
    return lines


# ------------------------------------------------------------------------- main

def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[1] not in ("size", "charge"):
        print(__doc__.splitlines()[0])
        print("usage: python python/P12/gates.py size|charge <file.json>")
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
