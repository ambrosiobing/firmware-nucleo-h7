"""P12's four gates against one set of cases.

**What P12's oracle is.** Not a capture and not a vector table: a set of cases
whose verdict is known by construction, because a gate's subject is its decision
rather than its arithmetic. The case that matters is the one the chapter states
outright, a six percent charge regression, which every implementation must fail
and must name the phase of. A gate that called that a pass would be worthless,
and four implementations agreeing on a wrong answer would still be wrong.

**The protocol is `projects/P12-energy-regression/PROTOCOL.md`**, written before
these implementations rather than inferred from whichever was read first. It also
records the two decisions worth arguing about: why the comparison is on the rules
with the case already parsed rather than on JSON, and why the doubles in an
answer are written in a round-tripping form rather than the two decimal places
each gate prints for a human.

**Where the Python sits in this.** The other five parity tests take the C as the
reference, because the C is the one that compiles for the target. Here the Python
is the reference, as it is in P03, and for a stronger reason than precedent: the
gate that actually runs is the Python one. It reads the committed `baseline.json`
and `budgets.json`, it is what a CI job invokes, and the other three exist to
show the rules are stated clearly enough to be implemented four times. So the
Python's verdict is the oracle and the canonical answer for it is rendered here,
in the harness, from the structured verdict `gates.py` returns. The C, C++ and
Rust each render their own, which is what makes a disagreement about the rules
visible rather than hidden behind a shared formatter.

`python/tests/test_gates.py` is the other half and is not replaced by this file.
It proves the gate's English, its JSON edge and the committed files, none of which
the other three implementations have at all.
"""
from __future__ import annotations

import ctypes
import sys
from pathlib import Path

import pytest

from conftest import (
    BUILD,
    GATES_CPP_FILTER,
    GATES_RUST_FILTER,
    assert_not_vacuous,
    run_filter,
    shared_library_name,
)

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "projects" / "P12-energy-regression" / "python"))

from gates import charge_verdict, size_verdict  # noqa: E402

LANGUAGES = ("C", "C++", "Python", "Rust")

# The same tolerance P06 settled on, and for the same reason: PROTOCOL.md says
# the doubles in an answer are compared as numbers and not as strings, so this is
# about arithmetic and not about four decimal printers. Nine orders of magnitude
# tighter than the five percent the gate rules on.
RELATIVE_TOLERANCE = 1e-12

GATE_NAME_MAX = 48          # must match GATE_NAME_MAX in c/gates.h
ANSWER_CAP = 8192


class CSize(ctypes.Structure):
    """Must match gate_size_t in projects/P12-energy-regression/c/gates.h."""

    _fields_ = [
        ("name", ctypes.c_char * GATE_NAME_MAX),
        ("flash", ctypes.c_long),
        ("static_ram", ctypes.c_long),
        ("has_flash", ctypes.c_bool),
        ("has_static_ram", ctypes.c_bool),
    ]


class CPhase(ctypes.Structure):
    """Must match gate_phase_t in projects/P12-energy-regression/c/gates.h."""

    _fields_ = [
        ("name", ctypes.c_char * GATE_NAME_MAX),
        ("microcoulombs", ctypes.c_double),
    ]


_LIB = None


def c_lib():
    """The C, which skips rather than fails when it has not been built.

    Compiling on win11 aquamarine is forbidden, so a skip naming the command and
    the laptop is the honest result there, as in every other parity test here.
    """
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("gates")
        if not path.exists():
            pytest.skip(
                "{} is missing, and this laptop does not compile. In WSL on "
                "bing@JPTOUPM678:\n    python3 python/tools/build_host.py".format(
                    path.name))
        d = ctypes.CDLL(str(path))
        d.gates_size_answer.restype = ctypes.c_int
        d.gates_size_answer.argtypes = [
            ctypes.POINTER(CSize), ctypes.c_size_t,
            ctypes.POINTER(CSize), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_size_t,
        ]
        d.gates_charge_answer.restype = ctypes.c_int
        d.gates_charge_answer.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(CPhase), ctypes.c_size_t,
            ctypes.c_bool, ctypes.c_double,
            ctypes.POINTER(CPhase), ctypes.c_size_t,
            ctypes.c_double,
            ctypes.c_char_p, ctypes.c_size_t,
        ]
        d.gate_status_name.restype = ctypes.c_char_p
        d.gate_status_name.argtypes = [ctypes.c_int]
        _LIB = d
    return _LIB


# ------------------------------------------------------------------ the cases

BASE = (("wake", 100.0), ("sense", 500.0), ("compute", 100.0),
        ("send", 600.0), ("sleep", 20.0))
BASE_TOTAL = sum(value for _, value in BASE)

BUDGETS = (("p01-first-light", 16384, 12288), ("p02-ring", 20480, 16384))


def with_phase(name, value, phases=BASE):
    """The baseline's phases with one changed, as a list of pairs."""
    return tuple((n, value if n == name else v) for n, v in phases)


def without_phase(name, phases=BASE):
    return tuple((n, v) for n, v in phases if n != name)


def total_of(phases):
    return sum(value for _, value in phases)


def size_case(name, measured, budgets=BUDGETS):
    return {"name": name, "kind": "size", "measured": measured, "budgets": budgets}


def charge_case(name, phases, baseline=BASE, build="v0.1-3-gdeadbee",
                total="auto", tolerance=0.05):
    return {"name": name, "kind": "charge", "phases": phases,
            "baseline": baseline, "build": build,
            "total": total_of(phases) if total == "auto" else total,
            "tolerance": tolerance}


def cases():
    """Every case, with the reason it is here.

    One case per rule, plus the ones that pin a boundary from both sides, plus
    the ones that pin the ORDER of a decision rather than its content.
    """
    out = []

    # ---- the size gate
    out.append(size_case("a build inside both budgets",
                         (("p01-first-light", 9000, 4000),)))
    out.append(size_case("flash over budget, which must say by how much",
                         (("p01-first-light", 20000, 4000),)))
    out.append(size_case("static_ram over budget",
                         (("p01-first-light", 9000, 99999),)))
    # Two failures from one target, which pins the order they are produced in:
    # flash before static_ram, which is the order a reader sees.
    out.append(size_case("both fields over budget, in field order",
                         (("p01-first-light", 20000, 99999),)))
    out.append(size_case("a target with no committed budget",
                         (("p99-new-thing", 100, 100),)))
    out.append(size_case("nothing reported at all, which is a refusal", ()))
    out.append(size_case("the build omitted a field, which is not a zero",
                         (("p01-first-light", 9000, None),)))
    out.append(size_case("the budget omits a field",
                         (("p01-first-light", 9000, 4000),),
                         (("p01-first-light", 16384, None),)))
    # Handed over in the wrong order on purpose. Three implementations sort and
    # the Python's dictionary is sorted when it is read, so an implementation
    # that did not sort would disagree here and nowhere else.
    out.append(size_case("two targets, handed over unsorted",
                         (("p02-ring", 1000, 1000), ("p01-first-light", 2000, 2000))))
    # A zero limit is the branch where the percentage cannot be computed. Every
    # implementation reports zero rather than dividing.
    out.append(size_case("a budget of zero, which must not be divided by",
                         (("p01-first-light", 0, 0),),
                         (("p01-first-light", 0, 0),)))
    out.append(size_case("over a budget of zero by one byte",
                         (("p01-first-light", 1, 0),),
                         (("p01-first-light", 0, 0),)))

    # ---- the charge gate
    out.append(charge_case("a run exactly on the baseline", BASE))
    # THE CASE THIS PROJECT EXISTS FOR, and the chapter states it outright.
    out.append(charge_case("six percent on send, which must turn the build red",
                           with_phase("send", 600.0 * 1.06)))
    # The boundary from the other side. A gate that failed everything would also
    # satisfy the case above.
    out.append(charge_case("four and a half percent on send, which must pass",
                           with_phase("send", 600.0 * 1.045)))
    out.append(charge_case("an improvement, which is the point of the work",
                           with_phase("send", 400.0)))
    out.append(charge_case("a missing phase, which looks like an improvement",
                           without_phase("send")))
    out.append(charge_case("a phase measured but not in the baseline",
                           BASE + (("radio", 900.0),)))
    # Both at once, which pins the production order: the baseline's phases in
    # name order first, then the measured ones.
    out.append(charge_case("one phase missing and one not in the baseline",
                           without_phase("send") + (("radio", 900.0),)))
    out.append(charge_case("a ledger with no build identity", BASE, build=None))
    out.append(charge_case("a ledger with no total to reconcile against",
                           BASE, total=None))
    out.append(charge_case("a ledger with no phases", ()))
    out.append(charge_case("phases that do not sum to the reported total",
                           BASE, total=BASE_TOTAL * 2))
    # THE ORDER OF THE REFUSALS, pinned. This ledger has no build identity AND a
    # total that does not reconcile. Every implementation must name the identity,
    # because a capture that cannot be tied to a firmware is not evidence
    # whatever its numbers say.
    out.append(charge_case("no build identity and a broken sum, identity first",
                           BASE, build=None, total=BASE_TOTAL * 2))
    # And the next one down: no phases and no total. no_phases must win.
    out.append(charge_case("no phases and no total, phases first",
                           (), total=None))
    # A baseline phase of zero is the branch where the change cannot be
    # computed as a percentage.
    out.append(charge_case("a baseline phase of zero, which must not be divided by",
                           (("idle", 0.0), ("send", 600.0)),
                           baseline=(("idle", 0.0), ("send", 600.0))))
    out.append(charge_case("a tolerance of zero, where any increase fails",
                           with_phase("send", 600.000001), tolerance=0.0))
    # Handed over unsorted, for the same reason as the size case above.
    out.append(charge_case("phases handed over unsorted",
                           tuple(reversed(BASE))))
    # Within the five percent slack the sum check allows, which is a pass and
    # not a refusal: the slack exists so a rounded total in a ledger does not
    # refuse an otherwise good run.
    out.append(charge_case("a total inside the sum check's own slack",
                           BASE, total=BASE_TOTAL * 1.02))

    return out


# ------------------------------------------------------- the four renderings

def request_line(case):
    """The case as PROTOCOL.md's one line."""
    def sizes(items):
        if not items:
            return "-"
        return ",".join(
            "{}:{}:{}".format(name,
                              "-" if flash is None else flash,
                              "-" if ram is None else ram)
            for name, flash, ram in items)

    def phases(items):
        if not items:
            return "-"
        return ",".join("{}:{!r}".format(name, value) for name, value in items)

    if case["kind"] == "size":
        return "S {} {}".format(sizes(case["measured"]), sizes(case["budgets"]))
    return "C {!r} {} {} {} {}".format(
        case["tolerance"],
        case["build"] if case["build"] else "-",
        "-" if case["total"] is None else repr(case["total"]),
        phases(case["phases"]),
        phases(case["baseline"]))


def canonical(failed, refusal, failures, report):
    """PROTOCOL.md's answer, assembled from its four fields."""
    return "{} {} {} {}".format(
        "FAIL" if failed else "PASS",
        refusal if refusal else "-",
        ",".join(failures) if failures else "-",
        ",".join(report) if report else "-")


def via_python(case):
    """The Python's verdict, rendered into the canonical answer.

    The rendering is here and not in `gates.py`, because the gate that runs has
    no use for it: it prints English for a person reading a failed build. This is
    the harness translating the oracle into the protocol, which is the same job
    P06's parity test does when it reads the C's struct.
    """
    def g(value):
        return repr(float(value))

    if case["kind"] == "size":
        measured = {}
        for name, flash, ram in case["measured"]:
            fields = {}
            if flash is not None:
                fields["flash"] = flash
            if ram is not None:
                fields["static_ram"] = ram
            measured[name] = fields
        budgets = {}
        for name, flash, ram in case["budgets"]:
            fields = {}
            if flash is not None:
                fields["flash"] = flash
            if ram is not None:
                fields["static_ram"] = ram
            budgets[name] = fields

        verdict = size_verdict(measured, budgets)
        if verdict["refused"] is not None:
            return canonical(True, verdict["refused"][0], [], [])
        failures = []
        for item in verdict["failures"]:
            if item[0] == "over":
                failures.append("over:{}:{}:{}:{}:{}".format(
                    item[1], item[2], item[3], item[4], item[5]))
            else:
                failures.append(":".join(str(part) for part in item))
        report = ["{}:{}:{}:{}:{}:{}".format(
            target, field, used, limit, g(pct), margin)
            for target, field, used, limit, pct, margin in verdict["report"]]
        return canonical(bool(failures), "", failures, report)

    ledger = {"phases": {name: value for name, value in case["phases"]}}
    if case["build"]:
        ledger["build"] = case["build"]
    if case["total"] is not None:
        ledger["total_uc"] = case["total"]
    baseline = {"phases": {name: value for name, value in case["baseline"]}}

    verdict = charge_verdict(ledger, baseline, case["tolerance"])
    if verdict["refused"] is not None:
        refused = verdict["refused"]
        if refused[0] == "sum_mismatch":
            return canonical(True, "sum_mismatch:{}:{}".format(
                g(refused[1]), g(refused[2])), [], [])
        return canonical(True, refused[0], [], [])

    failures = []
    for item in verdict["failures"]:
        if item[0] == "over":
            failures.append("over:{}:{}:{}:{}".format(
                item[1], g(item[2]), g(item[3]), g(item[4])))
        else:
            failures.append("{}:{}".format(item[0], item[1]))
    report = ["{}:{}:{}:{}".format(phase, g(got), g(want), g(change))
              for phase, got, want, change in verdict["report"]]
    return canonical(bool(failures), "", failures, report)


def via_c(case):
    """The C, through ctypes, with the case already parsed.

    The C is handed arrays rather than a line, so unlike the C++ and the Rust its
    parsing is not under test. PROTOCOL.md says why the gate and not a parser is
    the subject here, and that asymmetry is the same one every other parity test
    in this repository has.
    """
    lib = c_lib()
    out = ctypes.create_string_buffer(ANSWER_CAP)

    if case["kind"] == "size":
        def pack(items):
            array = (CSize * max(1, len(items)))()
            for i, (name, flash, ram) in enumerate(items):
                assert len(name) < GATE_NAME_MAX, name
                array[i].name = name.encode("ascii")
                array[i].has_flash = flash is not None
                array[i].flash = 0 if flash is None else flash
                array[i].has_static_ram = ram is not None
                array[i].static_ram = 0 if ram is None else ram
            return array

        measured = pack(case["measured"])
        budgets = pack(case["budgets"])
        status = lib.gates_size_answer(
            measured, len(case["measured"]), budgets, len(case["budgets"]),
            out, ANSWER_CAP)
    else:
        def pack(items):
            array = (CPhase * max(1, len(items)))()
            for i, (name, value) in enumerate(items):
                assert len(name) < GATE_NAME_MAX, name
                array[i].name = name.encode("ascii")
                array[i].microcoulombs = value
            return array

        phases = pack(case["phases"])
        baseline = pack(case["baseline"])
        build = (case["build"] or "").encode("ascii")
        status = lib.gates_charge_answer(
            build, phases, len(case["phases"]),
            case["total"] is not None,
            0.0 if case["total"] is None else case["total"],
            baseline, len(case["baseline"]),
            case["tolerance"], out, ANSWER_CAP)

    assert status == 0, "the C refused the call itself: {}".format(
        lib.gate_status_name(status).decode("ascii"))
    return out.value.decode("ascii")


def via_filter(path, all_cases):
    return run_filter(path, [request_line(case) for case in all_cases])


def available(all_cases):
    """The implementations built on this laptop, each over the same cases.

    The presence of each is decided by looking for its artefact rather than by
    calling it and catching a skip. `pytest.skip` raises from `BaseException`, so
    an `except Exception` around `c_lib()` would not catch it and this function
    would skip the whole test instead of reporting that the C is absent, which is
    precisely the information `assert_not_vacuous` exists to act on.
    """
    present = {"Python": [via_python(c) for c in all_cases]}
    if (BUILD / shared_library_name("gates")).exists():
        present["C"] = [via_c(c) for c in all_cases]
    for name, path in (("C++", GATES_CPP_FILTER), ("Rust", GATES_RUST_FILTER)):
        if path.exists():
            present[name] = via_filter(path, all_cases)
    return present


# ------------------------------------------------------------ the comparison

def same_number(a, b):
    """Two doubles from PROTOCOL.md's answer, compared as numbers.

    They are written in whatever form round-trips in each language, so this
    parses rather than compares text. The tolerance is for the arithmetic, not
    for the printing.
    """
    try:
        x, y = float(a), float(b)
    except ValueError:
        return a == b
    if x == y:
        return True
    scale = max(abs(x), abs(y))
    return abs(x - y) <= RELATIVE_TOLERANCE * scale


def same_answer(a, b):
    """One answer against another, field by field.

    The verdict, the refusal's name, the failure codes, every name and every
    integer are compared exactly. Only the doubles go through the tolerance, and
    they are recognised by being parseable as a float, which is why the codes and
    the names are compared before the numbers rather than after.
    """
    if a == b:
        return True
    left, right = a.split(" "), b.split(" ")
    if len(left) != len(right):
        return False
    for one, two in zip(left, right):
        if one == two:
            continue
        # Within a field, items are separated by commas and parts by colons.
        if one.count(",") != two.count(","):
            return False
        for item_a, item_b in zip(one.split(","), two.split(",")):
            parts_a, parts_b = item_a.split(":"), item_b.split(":")
            if len(parts_a) != len(parts_b):
                return False
            for part_a, part_b in zip(parts_a, parts_b):
                if part_a != part_b and not same_number(part_a, part_b):
                    return False
    return True


def test_every_language_reaches_the_same_verdict_on_every_case(capsys):
    all_cases = cases()
    present = available(all_cases)
    assert_not_vacuous(present, "P12")

    reference = present["Python"]
    for language, answers in sorted(present.items()):
        assert len(answers) == len(all_cases), (
            "{} answered {} of {} requests".format(
                language, len(answers), len(all_cases)))

    problems = []
    for i, case in enumerate(all_cases):
        for language, answers in sorted(present.items()):
            if language == "Python":
                continue
            if not same_answer(reference[i], answers[i]):
                problems.append(
                    "case {!r}\n  Python {}\n  {:<6} {}".format(
                        case["name"], reference[i], language, answers[i]))
    assert not problems, "\n".join(problems)

    with capsys.disabled():
        print("\n  P12 parity over {} cases, {} size and {} charge".format(
            len(all_cases),
            sum(1 for c in all_cases if c["kind"] == "size"),
            sum(1 for c in all_cases if c["kind"] == "charge")))
        for language in LANGUAGES:
            state = "agrees" if language in present else "not built, not compared"
            print("    {:<7} {}".format(language, state))


def test_the_chapters_criterion_fails_in_every_language():
    """A six percent charge regression turns the build red, asserted by name.

    This is the one case the chapter states outright, and it is asserted here
    rather than left to the general comparison, because four implementations
    agreeing that it passes would still be wrong.
    """
    case = [c for c in cases() if "six percent" in c["name"]]
    assert len(case) == 1
    present = available(case)
    assert_not_vacuous(present, "P12")
    for language, answers in sorted(present.items()):
        answer = answers[0]
        assert answer.startswith("FAIL "), "{} said {}".format(language, answer)
        assert "over:send:" in answer, "{} said {}".format(language, answer)


def test_the_comparison_can_tell_two_answers_apart():
    """The comparison itself, checked, because it is bespoke.

    `same_answer` has to accept two spellings of one double and reject a real
    disagreement, and a version that accepted everything would make every other
    assertion in this file pass without comparing anything. So it is tested the
    way the gates are: on cases whose verdict is known.
    """
    answer = ("FAIL - over:send:636.0:600.0:6.0 "
              "compute:100.0:100.0:0.0,send:636.0:600.0:6.0")

    # The same answer with the doubles spelled differently, which is the whole
    # reason this function parses rather than compares text: the C writes %.17g
    # and Rust writes its shortest round-tripping form.
    assert same_answer(answer, answer.replace("636.0", "6.3600000000000000e2"))
    assert same_answer(answer, answer.replace("600.0", "600.00000000000000"))
    assert same_answer(answer, answer)

    # And every way of being wrong that matters.
    wrong = {
        "the verdict": answer.replace("FAIL", "PASS"),
        "the phase named": answer.replace("over:send", "over:sense"),
        "the failure code": answer.replace("over:send", "missing_phase:send"),
        "a double, past the tolerance": answer.replace("636.0", "637.0"),
        "a double, just past the tolerance": answer.replace(
            "over:send:636.0", "over:send:636.00000001"),
        "a refusal appearing": answer.replace("FAIL -", "FAIL no_build"),
        "a failure dropped": answer.replace("over:send:636.0:600.0:6.0 ", "- "),
        "a report row dropped": answer.replace(
            ",send:636.0:600.0:6.0", ""),
        "the report order": answer.replace(
            "compute:100.0:100.0:0.0,send:636.0:600.0:6.0",
            "send:636.0:600.0:6.0,compute:100.0:100.0:0.0"),
    }
    for what, other in sorted(wrong.items()):
        assert not same_answer(answer, other), (
            "the comparison cannot tell {} apart:\n  {}\n  {}".format(
                what, answer, other))


def test_the_case_set_exercises_every_rule_and_every_refusal():
    """The adequacy of the case set, checked in Python so it runs everywhere.

    A parity suite over cases that never reach a rule proves nothing about that
    rule, however many languages agree. This asserts that every refusal and every
    failure code in PROTOCOL.md occurs at least once, and that at least one case
    passes each gate, so the set cannot quietly stop covering a rule.
    """
    answers = [via_python(case) for case in cases()]
    seen = set()
    passes = {"size": 0, "charge": 0}
    for case, answer in zip(cases(), answers):
        verdict, refusal, failures, _ = answer.split(" ")
        if verdict == "PASS":
            passes[case["kind"]] += 1
        if refusal != "-":
            seen.add(refusal.split(":")[0])
        if failures != "-":
            for item in failures.split(","):
                seen.add(item.split(":")[0])

    want = {"no_sizes", "no_budget", "no_field", "no_field_budget", "over",
            "no_build", "no_phases", "no_total", "sum_mismatch",
            "missing_phase", "not_in_baseline"}
    assert want <= seen, "these rules are never reached: {}".format(
        ", ".join(sorted(want - seen)))
    assert passes["size"] >= 1 and passes["charge"] >= 1, (
        "a gate that failed every case would satisfy the comparison, so each "
        "gate needs at least one case that passes: {}".format(passes))
