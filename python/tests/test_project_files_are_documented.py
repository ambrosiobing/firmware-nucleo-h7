"""Every file a project directory holds is named by that project's own README.

WHY THIS EXISTS, AND IT IS NOT A STYLE RULE. On Sunday 4 October 2026 commit
30c3011 deleted a sixty line section from
projects/P01-toolchain-first-light/README.md and nobody noticed, including the
person who did it. The section was the entire write-up of that project's oracle:
what clock_vectors.json is, how its rows were double entered, which three rows
earn their place, and the three mutations that proved the tests can fail. It went
because a scripted edit replaced the span between two headings and the section
was sitting between them, so the slice was never read.

Every check in this repository passed. check_links.py checks that links resolve
and all of them still did. check_status.py checks that paths NAMED in a README
exist, which is the opposite direction: it catches a name with no file and is
blind to a file with no name. The status table was fine, the suite was green, and
the commit went to main with a deleted chapter in it.

So this closes the other direction. A file that exists in a project directory and
is named nowhere in that project's README is either undocumented or newly
orphaned by an edit, and both are worth a red test. It is deliberately the
project's OWN README.md and not any Markdown in the tree: when the section was
deleted, cpp/README.md, python/README.md and rust/README.md all still linked to
clock_vectors.json, so a looser rule would have stayed green.

It found one thing on the day it was written, which is the other argument for it:
Find-STM32Tooling.ps1 had been in P01 since the toolchain inventory and no README
mentioned it.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from conftest import ROOT

PROJECTS = sorted(p for p in (ROOT / "projects").iterdir() if p.is_dir())

# Files whose names a README is expected to carry. Source files under c/, cpp/,
# python/ and rust/ are not included: those are listed by the language READMEs
# and by the build, and requiring every .c file in every project README would be
# a rule about prose length rather than about documentation.
DOCUMENTED_SUFFIXES = {".json", ".md", ".py", ".csv", ".ps1", ".toml", ".yaml", ".yml"}


def candidates(project: Path):
    return [f for f in sorted(project.iterdir())
            if f.is_file()
            and f.name != "README.md"
            and f.suffix.lower() in DOCUMENTED_SUFFIXES]


def test_there_are_twenty_projects_to_check():
    """The guard against this file quietly checking nothing."""
    assert len(PROJECTS) == 20, "expected twenty project directories, found {}".format(
        len(PROJECTS))
    with_files = [p.name for p in PROJECTS if candidates(p)]
    assert len(with_files) >= 4, (
        "only {} projects hold a file this test would check, which is too few for "
        "it to be meaningful: {}".format(len(with_files), ", ".join(with_files)))


@pytest.mark.parametrize("project", PROJECTS, ids=[p.name for p in PROJECTS])
def test_every_file_in_a_project_is_named_by_that_projects_readme(project):
    readme = project / "README.md"
    assert readme.is_file(), "{} has no README.md".format(project.name)
    text = readme.read_text(encoding="utf-8")

    missing = [f.name for f in candidates(project) if f.name not in text]
    assert not missing, (
        "{}/README.md does not name {}. Either the file is undocumented, or an "
        "edit removed the part of the README that described it, which is what "
        "happened to clock_vectors.json in commit 30c3011.".format(
            project.name, ", ".join(missing)))
