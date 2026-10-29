"""No workflow script carries a literal backslash-n, and every one can be run.

**This file exists because of one red commit and a three day wrong diagnosis.**
On Monday 26 October 2026 commit 9208639 added eight compiler invocations to
`code.yml`'s warnings-as-errors step and main went red. Every one of those eight
sources had been compiled on the laptop with a compiler, under the exact flag
string, and all eight were clean.

On Thursday 29 October 2026 the cause was found in the committed file:
**all eight added lines carried a literal backslash-n where a YAML line
continuation was intended.** Bash reads that as an escaped `n` outside quotes,
so each gcc was handed an argument `n`, found no such file, and exited 1. The
step would have failed under every compiler, which is why the two sources still
in it pass under gcc 13, 14 and 15 alike.

For three days the explanation on record was that CI's gcc warned where gcc 15
does not. It was wrong, and nothing in the repository could have contradicted
it: the step's text was never run anywhere except on the runner.

**Two things are checked here, and neither needs a compiler.**

A literal backslash-n in a workflow script is refused outright. It has no
legitimate use in any script this repository writes, and it is the exact shape
of the defect. The trap it comes from has cost this volume nine repairs, always
the same way: a tool layer consumes a real newline and leaves the two
characters behind.

And every line that ends in a backslash must be a real continuation, which
means another line follows it. A backslash at the end of the last line of a
script is a continuation into nothing.

`python/tools/run_workflow_step.py` is the other half of the remedy: it reads a
step out of the workflow and runs exactly those bytes, under a chosen gcc. A
test cannot do that here, because nothing compiles on win11 aquamarine, but the
hand-over can and now does.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"

sys.path.insert(0, str(ROOT / "python" / "tools"))

from run_workflow_step import steps_in  # noqa: E402

BACKSLASH = chr(92)
LITERAL_NEWLINE_ESCAPE = BACKSLASH + "n"

# A backslash followed by anything but the end of the line is not a
# continuation, and the only one this repository means is the end-of-line form.
TRAILING = re.compile(re.escape(BACKSLASH) + r"$")


def workflow_files():
    found = sorted(WORKFLOWS.glob("*.yml"))
    assert found, "no workflow files were found, so this test reads nothing"
    return found


@pytest.mark.parametrize("path", workflow_files(), ids=lambda p: p.name)
def test_no_workflow_script_carries_a_literal_backslash_n(path):
    """The defect of commit 9208639, refused by its shape."""
    problems = []
    for name, script in steps_in(path):
        for number, line in enumerate(script.split("\n"), start=1):
            if LITERAL_NEWLINE_ESCAPE in line:
                problems.append(
                    "{} step {!r} line {}: {!r}"
                    .format(path.name, name, number, line.strip()[:72]))
    assert not problems, (
        "a literal backslash followed by n appears in a workflow script. Bash "
        "reads that as an escaped n outside quotes, so the command is handed "
        "an argument named n and fails under every compiler. That is what "
        "turned main red on Monday 26 October 2026 and was diagnosed as a "
        "compiler difference for three days: " + "; ".join(problems))


@pytest.mark.parametrize("path", workflow_files(), ids=lambda p: p.name)
def test_every_trailing_backslash_continues_onto_another_line(path):
    """A continuation into nothing is a command that loses its last argument."""
    problems = []
    for name, script in steps_in(path):
        lines = script.split("\n")
        for number, line in enumerate(lines, start=1):
            if not TRAILING.search(line.rstrip("\n")):
                continue
            following = lines[number] if number < len(lines) else ""
            if following.strip() == "":
                problems.append("{} step {!r} line {}"
                                .format(path.name, name, number))
    assert not problems, (
        "a script line ends in a backslash with nothing after it, so the "
        "continuation joins onto a blank line and the command loses whatever "
        "was meant to follow: " + "; ".join(problems))


def test_the_extractor_reads_the_workflows_own_bytes():
    """The tool is only worth having if what it runs is what is committed.

    Every non-blank line it hands back must appear in the workflow file at the
    block's own indentation. Without this the tool could normalise the very
    thing it exists to expose.
    """
    checked = 0
    for path in workflow_files():
        text = path.read_text(encoding="utf-8")
        for name, script in steps_in(path):
            for line in script.split("\n"):
                if not line.strip():
                    continue
                assert ("          " + line) in text, (
                    "{} step {!r}: the extractor produced a line that is not "
                    "in the file at the block indentation, so it is not "
                    "reading the committed bytes: {!r}"
                    .format(path.name, name, line[:72]))
                checked += 1
    assert checked > 60, (
        "only {} script lines were checked, which is fewer than the three "
        "workflows hold, so the extractor has stopped finding steps"
        .format(checked))
    print("\n  {} script lines across {} workflows, every one read verbatim "
          "from the committed file".format(checked, len(workflow_files())))
