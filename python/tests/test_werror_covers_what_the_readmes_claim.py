"""A README that claims warnings as errors names a file the workflow compiles so.

**What this is protecting, and the day it was needed.** On Monday 26 October 2026
eleven README files across six projects said their C was compiled with
`-Wconversion`, `-Wsign-conversion` and the rest as errors, and P04's opening
paragraph said it of CI outright. The warnings-as-errors step in
`.github/workflows/code.yml` was a hand-maintained list that had not grown since
Sunday 4 October 2026, and it named none of the eight C sources written that day.
Five of those eight carried the claim. It was backed by nothing.

That is the drift `check_status.py` exists to stop, one layer in: a status line is
checked against the source tree and the table, and a sentence in the body of a
README was checked against nothing. The suite already refuses a project whose
status claims more than its code, a coverage figure that disagrees with the suite
size, and a language README that denies its own code. This refuses a claim about
the compiler that the compiler was never asked to make.

**The first attempt to satisfy it by adding those eight sources to the step turned
main red**, because all eight compile clean under gcc 15.2.0 in WSL and the CI
runner's gcc is a different version. The nine claims were corrected instead, to
say that CI passes the whole warning set over those sources without `-Werror`,
which is true. Making the stronger claim true again waits on that diagnostic, and
it cannot be verified on the demo laptop: a change to a workflow step is answered
only by the runner.

**THE STEP STOPPED BEING A LIST ON SUNDAY 1 NOVEMBER 2026.** It now loops over
`check_c_syntax.py --list`, so every host-compilable C source is compiled with
`-Werror` by existing rather than by being named, and the five it used to name
became forty-one. This test therefore reads the step two ways: the discovered
set, computed from the same function the step calls, and the sources still named
explicitly, which are the seven C++ filters.

**The first figure was forty-three and it was measured with the wrong verb.** The
survey compiled with `-fsyntax-only`, which stops before the assembler; the step
compiles with `-c`, which runs it. Two of the forty-three hold Cortex-M `dsb` and
`isb`, so run 41324507888 failed at the third file and forty of the forty-three
were never attempted. Re-measured with `-c` the answer is forty-one clean, two
that the host assembler cannot take, and two whose pointer width differs, which
is the arithmetic the four exclusions below carry.

**What it cannot do.** It reads a workflow and some Markdown, so it sees
spellings. It does not know whether the step it reads is the step that runs, and
it cannot tell whether a file compiles clean: that needs a compiler, and the
authoring laptop has none by rule. The version difference above is the sharp edge
of that limit and is not closed by anything here. What it can say is that nobody
claims a guarantee the workflow was not asked for.
"""
from __future__ import annotations

import importlib.util
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "code.yml"
PROJECTS = ROOT / "projects"
CHECKER = ROOT / "python" / "tools" / "check_c_syntax.py"

# The step that compiles with -Werror, by its name in the workflow.
STEP_NAME = "The same sources with warnings as errors"

# How the step discovers its C half, and how it names the exceptions.
#
# THE LOOP AND NOT THE WORDS. The first version of this looked for the bare
# string "check_c_syntax.py --list" anywhere in the step, and the step's own
# comment contains that phrase while explaining itself, so replacing the loop
# with a hand-written file list left this test green. Proved by doing it on
# Sunday 1 November 2026, which is the only reason it is written this way. A
# pattern must not be satisfiable by the prose beside the thing it checks.
DISCOVERS = re.compile(
    r"for\s+\w+\s+in\s+\$\(\s*python3?\s+python/tools/check_c_syntax\.py"
    r"\s+--list\s*\)")
DISCOVERS_SHAPE = "for f in $(python python/tools/check_c_syntax.py --list)"
EXCLUDE_LINE = re.compile(r'^\s*EXCLUDE="([^"]*)"', re.M)

# The four the survey of Sunday 1 November 2026 could not compile on a host, in
# two classes: two because this host's pointers are wider than the target's, and
# two because the host assembler is not a Cortex-M assembler. The step says why
# beside each, and the second class was found by the runner rather than by the
# survey, because the survey used -fsyntax-only and the step uses -c.
EXPECTED_EXCLUSIONS = {
    "c/board/board.c",
    "projects/P06-timer-sampling/c/main.c",
    "c/board/icache.c",
    "c/board/system.c",
}

# How a README claims the guarantee. A corrected README must not contain this
# phrase even in a denial, which is why the nine corrected on Monday
# 26 October 2026 say "which CI passes over it" and name the step with hyphens.
CLAIMS = ("as errors",)

# The whole token after -c, filtered by suffix afterwards. The first version of
# this matched r"\S+\.(?:c|cpp)", and an alternation takes ".c" before ".cpp",
# so gates_filter.cpp was captured as gates_filter.c and reported missing from a
# step that compiles it. That is the error of matching the letters of "float"
# inside the word "floating", which produced a wrong audit the same day: a
# pattern that can match less than the token it is aimed at.
SOURCE_IN_STEP = re.compile(r"-c\s+(\S+)")
SOURCE_SUFFIXES = (".c", ".cpp")


def step_body():
    text = WORKFLOW.read_text(encoding="utf-8")
    at = text.find(STEP_NAME)
    assert at > 0, (
        "the workflow has no step named {!r}, so either it was renamed or the "
        "warnings-as-errors step is gone. Either way this test is reading the "
        "wrong thing and must be updated deliberately".format(STEP_NAME))
    rest = text[at:]
    end = rest.find("\n      - name:")
    return rest if end < 0 else rest[:end]


def host_compilable():
    """The C files the step's own discovery hands it.

    Imported from the checker rather than reimplemented, because the whole point
    of the change on Sunday 1 November 2026 was that there is one definition of
    this set. A second copy here would be the drift this file exists to refuse,
    one layer further in.
    """
    spec = importlib.util.spec_from_file_location("check_c_syntax", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    keep, _ = mod.host_compilable()
    return set(keep)


def excluded():
    body = step_body()
    found = EXCLUDE_LINE.search(body)
    assert found, (
        "the warnings-as-errors step has no EXCLUDE= line. If the exclusions "
        "were removed because both files now compile clean, delete "
        "EXPECTED_EXCLUSIONS here in the same commit and say so")
    return {p for p in found.group(1).split() if p}


def werror_sources():
    """Every source the warnings-as-errors step compiles, by repository path."""
    body = step_body()
    named = {m.group(1) for m in SOURCE_IN_STEP.finditer(body)
             if m.group(1).endswith(SOURCE_SUFFIXES)}
    if not DISCOVERS.search(body):
        return named
    return named | (host_compilable() - excluded())


def readmes_of(project):
    for path in (project / "README.md", project / "c" / "README.md",
                 project / "cpp" / "README.md"):
        if path.is_file():
            yield path


def test_every_readme_claim_of_warnings_as_errors_is_in_the_workflow():
    covered = werror_sources()
    assert len(covered) >= 10, (
        "only {} sources were found in the warnings-as-errors step, which is "
        "fewer than the ten there on Sunday 4 October 2026, so this test is no "
        "longer reading that step correctly".format(len(covered)))

    claiming = []
    problems = []
    for project in sorted(p for p in PROJECTS.iterdir() if p.is_dir()):
        sources = sorted(
            s.relative_to(ROOT).as_posix()
            for d in ("c", "cpp")
            for s in (project / d).glob("*.c*")
            if s.is_file())
        if not sources:
            continue
        for readme in readmes_of(project):
            text = readme.read_text(encoding="utf-8")
            if not any(claim in text for claim in CLAIMS):
                continue
            claiming.append(readme.relative_to(ROOT).as_posix())
            missing = [s for s in sources if s not in covered]
            if missing:
                problems.append("{} claims warnings as errors and the "
                                "workflow's step does not compile {}"
                                .format(readme.relative_to(ROOT).as_posix(),
                                        ", ".join(missing)))
            break

    assert claiming, (
        "no README claims warnings as errors any more, so this test is "
        "guarding nothing. That is a real possibility rather than a failure, "
        "and it should be removed deliberately rather than left passing over "
        "an empty set")
    assert not problems, (
        "a README claims a guarantee the workflow was never asked to make:\n  "
        + "\n  ".join(problems))
    print("\n  {} sources compiled with warnings as errors; {} project(s) claim "
          "it and every one is covered".format(len(covered), len(claiming)))


def test_the_step_excludes_only_the_two_sources_it_explains():
    """A third exclusion has to be a visible act.

    The loop covers a new C file by existing, which is its purpose, and the one
    way to remove a file from the sweep is to add it to EXCLUDE. That is how a
    real warning would be quieted, so the list is pinned here and a change to it
    fails until this test is edited in the same commit, next to the step's own
    paragraph saying why each file is out.
    """
    assert DISCOVERS.search(step_body()), (
        "the warnings-as-errors step no longer runs its C sweep as "
        "{!r}, so it has gone back to being a hand-maintained list and the "
        "drift of Monday 26 October 2026 is available again"
        .format(DISCOVERS_SHAPE))

    out = excluded()
    assert out == EXPECTED_EXCLUSIONS, (
        "the step's exclusions are {} and this test expects {}. An exclusion "
        "removes a source from the only thing compiling it with -Werror, so "
        "adding one is a deliberate act: put the reason in the step beside the "
        "other two and change EXPECTED_EXCLUSIONS here"
        .format(sorted(out), sorted(EXPECTED_EXCLUSIONS)))

    # An exclusion naming a file the discovery never offers covers nothing and
    # would sit here reading as though it did.
    listed = host_compilable()
    stale = sorted(p for p in out if p not in listed)
    assert not stale, (
        "these are excluded from the warnings-as-errors sweep but are not in "
        "the set it sweeps, so the exclusion is inert and misleading: {}"
        .format(", ".join(stale)))

    print("\n  {} C sources swept with warnings as errors, {} excluded with a "
          "reason each".format(len(listed - out), len(out)))
