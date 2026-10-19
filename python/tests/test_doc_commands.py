"""check_doc_commands: a documented command naming a path that does not exist.

**The defects this pins.** Three of them, all found on Sunday 18 October 2026 by
asking whether the commands in this volume's own pages still work. P09's README
offered `python -m pytest tests -q` where this suite is `python/tests`, with a figure
of twenty-one checks beside it that no command could produce; P12's README named
`python/gates.py` twice for a program at
`projects/P12-energy-regression/python/gates.py`, whose own usage message carried the
right path; and chapter 9's build recipe named two paths from the layout that existed
before every project owned its four languages.

**Why this class earned a checker and another the same day did not.** A check for
comments claiming something is "in this file" was declined: two sites, one wrong, and
it would have had to tell a quoted claim from a live one. This class had 36 paths
across 136 documents with three wrong, and "does this path exist" needs no judgement.

**What the tests assert.** Both directions, since a check that has never failed is
indistinguishable from one that cannot: a missing path is reported and a present one
is not. Then the three ways a token is not a path, flags, shell constructs and the
Windows drive letter that opens every hand-over command in this repository. Then that
several paths in one `pytest` invocation are all taken, which is how the suite is
usually run. Then the line number, because a report without one sends a reader
searching. And the regression guard, that the repository today has none missing.

`tempfile.mkdtemp` rather than pytest's `tmp_path`, whose `pytest-current` symlink
win11 aquamarine refuses, as `test_rate_parity.py` has recorded since Sunday
4 October 2026.
"""
from __future__ import annotations

import pathlib
import shutil
import sys
import tempfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python" / "tools"))

import check_doc_commands as checker  # noqa: E402


@pytest.fixture
def tree():
    d = pathlib.Path(tempfile.mkdtemp(prefix="doc-commands-"))
    try:
        yield d
    finally:
        shutil.rmtree(d, ignore_errors=True)


def write(tree, name, body):
    p = tree / name
    p.write_text(body, encoding="utf-8")
    return p


def test_a_missing_path_is_reported(tree, monkeypatch):
    """The defect itself: the suite is python/tests and the command said tests."""
    md = write(tree, "a.md", "Run it:\n\n```bash\npython -m pytest tests -q\n```\n")
    monkeypatch.setattr(checker, "ROOT", tree)
    found = checker.claims(md)
    assert [tok for tok, _ln in found] == ["tests"]
    assert not (tree / "tests").exists()


def test_a_present_path_is_not_a_defect(tree, monkeypatch):
    """The other direction, which is what makes the test above mean something."""
    (tree / "python").mkdir()
    (tree / "python" / "tests").mkdir()
    md = write(tree, "b.md", "```bash\npython -m pytest python/tests -q\n```\n")
    monkeypatch.setattr(checker, "ROOT", tree)
    found = checker.claims(md)
    assert [tok for tok, _ln in found] == ["python/tests"]
    assert all((tree / tok).exists() for tok, _ln in found)


def test_a_flag_is_not_a_path(tree, monkeypatch):
    md = write(tree, "c.md", "```bash\npython -m pytest python/tests -q -s\n```\n")
    monkeypatch.setattr(checker, "ROOT", tree)
    assert [tok for tok, _ln in checker.claims(md)] == ["python/tests"]


def test_the_windows_cd_path_is_skipped(tree, monkeypatch):
    """Every hand-over command in this repository opens with a drive letter, and
    those paths are the reader's machine rather than this repository's to check."""
    md = write(tree, "d.md",
               "```bash\ncd C:\\Users\\aquamarine\\Desktop\\firmware-nucleo-h7; "
               "python python/tools/check_links.py\n```\n")
    monkeypatch.setattr(checker, "ROOT", tree)
    toks = [tok for tok, _ln in checker.claims(md)]
    assert toks == ["python/tools/check_links.py"], toks


def test_several_paths_in_one_invocation_are_all_taken(tree, monkeypatch):
    md = write(tree, "e.md",
               "```bash\npython -m pytest python/tests/test_one.py "
               "python/tests/test_two.py -q\n```\n")
    monkeypatch.setattr(checker, "ROOT", tree)
    toks = [tok for tok, _ln in checker.claims(md)]
    assert toks == ["python/tests/test_one.py", "python/tests/test_two.py"], toks


def test_the_line_number_is_the_command_s_line(tree, monkeypatch):
    md = write(tree, "f.md", "one\ntwo\nthree\n\n```bash\npython -m pytest tests -q\n```\n")
    monkeypatch.setattr(checker, "ROOT", tree)
    found = checker.claims(md)
    assert found == [("tests", 6)], found


def test_every_documented_path_in_this_repository_exists():
    """The regression guard. Three were wrong on Sunday 18 October 2026."""
    missing = []
    for md in checker.documents():
        for token, line in checker.claims(md):
            if not (checker.ROOT / token).exists():
                missing.append("%s:%d names %s"
                               % (md.relative_to(checker.ROOT).as_posix(), line, token))
    assert not missing, "documented commands naming paths that do not exist: %s" % missing
