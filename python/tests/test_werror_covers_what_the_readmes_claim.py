"""A README that claims warnings as errors names a file the workflow compiles so.

**What this is protecting, and the day it was needed.** On Monday 26 October 2026
four project READMEs claimed their C was compiled with `-Wconversion`,
`-Wsign-conversion` and the rest "as errors", and P04's opening paragraph said
"compiled with warnings as errors in CI". None of those four sources appeared in
the warnings-as-errors step of `.github/workflows/code.yml`, which is a
hand-maintained list of ten files that had not grown since Sunday 4 October 2026.
The claims were not backed by anything.

That is the same drift `check_status.py` exists to stop, in a place it does not
look: a status line is checked against the source tree and the table, and a
sentence inside the body of a README is checked against nothing. The suite already
refuses a project whose status claims more than its code, a figure that disagrees
with the suite size, and a language README that denies its own code. This refuses
a claim about the compiler that the compiler was never asked to make.

**What it cannot do.** It reads a workflow and some Markdown, so it sees
spellings. It does not know whether the step it reads is the step that runs, and
it cannot tell whether a file compiles clean: that needs a compiler, and the
authoring laptop has none by rule. What it can say is that nobody is claiming a
guarantee the workflow does not ask for.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "code.yml"
PROJECTS = ROOT / "projects"

# The step that compiles with -Werror, by its name in the workflow.
STEP_NAME = "The same sources with warnings as errors"

# How a README claims the guarantee. Each is a phrase that only makes sense if
# some workflow step passes -Werror over that project's sources.
CLAIMS = ("as errors", "warnings as errors", "with warnings as errors")

# The whole token after -c, filtered by suffix afterwards. The first
# version of this matched r"\S+\.(?:c|cpp)", and the alternation takes
# ".c" before ".cpp", so gates_filter.cpp was captured as
# gates_filter.c and reported as missing from a step that compiles it.
# That is the same error as matching the letters of "float" inside
# "floating", which cost a wrong audit on Monday 26 October 2026: a
# pattern that can match less than the token it is aimed at.
SOURCE_IN_STEP = re.compile(r"-c\s+(\S+)")
SOURCE_SUFFIXES = (".c", ".cpp")


def werror_sources():
    """Every source the warnings-as-errors step compiles, by repository path."""
    text = WORKFLOW.read_text(encoding="utf-8")
    at = text.find(STEP_NAME)
    assert at > 0, (
        "the workflow has no step named {!r}, so either it was renamed or the "
        "warnings-as-errors step is gone. Either way this test is reading the "
        "wrong thing and must be updated deliberately".format(STEP_NAME))
    # The step runs until the next step's "- name:" at the same indentation.
    rest = text[at:]
    end = rest.find("\n      - name:")
    body = rest if end < 0 else rest[:end]
    return {m.group(1) for m in SOURCE_IN_STEP.finditer(body)
            if m.group(1).endswith(SOURCE_SUFFIXES)}


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
        "guarding nothing and should be removed deliberately rather than left "
        "passing")
    assert not problems, (
        "a README claims a guarantee the workflow was never asked to make:\n  "
        + "\n  ".join(problems))
    print("\n  {} sources compiled with warnings as errors; {} README(s) claim "
          "it and every one is covered".format(len(covered), len(claiming)))
