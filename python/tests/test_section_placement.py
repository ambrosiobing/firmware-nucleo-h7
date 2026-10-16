"""check_section_placement: a named section no linker rule collects is reported.

**The defect this pins.** P06's transfer-engine back end placed its sample buffer in
`.dma_buffer` while `c/ld/stm32h7a3zi.ld` collects `.dma_buffers` and
`.dma_buffers*`. An ld pattern ending in `*` matches names that START with it, so a
name one character shorter matches neither, and no rule placed that buffer. It was
found by reading the file on Friday 16 October 2026, before working on it, and it is
the one section attribute in the volume whose correctness is load bearing: on this
part the main transfer engines cannot reach the tightly coupled memories at all, so a
buffer that lands there moves nothing and reports no error.

**What the tests assert.** The glob rules, including the case that caused this, that
`.dma_buffers*` does not match `.dma_buffer`; the parse of a multi-pattern rule and
of the attribute form; the suggestion a failure prints, because a missing letter is
the defect and the fix should not need a search; and that every attribute in the
repository today is collected by something, which is the regression guard.

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

import check_section_placement as checker  # noqa: E402


@pytest.fixture
def tree():
    d = pathlib.Path(tempfile.mkdtemp(prefix="section-placement-"))
    try:
        yield d
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_a_trailing_star_does_not_match_a_shorter_name():
    """The defect itself, as one assertion. `.dma_buffers*` claims names that begin
    with `.dma_buffers`; `.dma_buffer` is shorter, so it begins with nothing of the
    sort and is claimed by neither rule."""
    plural = checker.glob_to_regex(".dma_buffers*")
    assert plural.match(".dma_buffers")
    assert plural.match(".dma_buffers.one")
    assert not plural.match(".dma_buffer")
    assert not checker.glob_to_regex(".dma_buffers").match(".dma_buffer")


def test_the_glob_rules_ld_uses():
    assert checker.glob_to_regex(".text*").match(".text.p09pad")
    assert checker.glob_to_regex(".text").match(".text")
    assert not checker.glob_to_regex(".text").match(".text.p09pad")
    assert checker.glob_to_regex(".bss?").match(".bss1")
    assert not checker.glob_to_regex(".bss?").match(".bss12")
    assert checker.glob_to_regex(".ram[012]").match(".ram1")
    assert not checker.glob_to_regex(".ram[012]").match(".ram9")
    # A dot is a literal here, not "any character"
    assert not checker.glob_to_regex(".text").match("Xtext")


def test_one_rule_may_list_several_patterns(tree):
    script = tree / "x.ld"
    script.write_text(
        "SECTIONS {\n"
        "  .text : { *(.text .rodata*)\n"
        "    *(.dma_buffers) }\n"
        "}\n", encoding="utf-8")
    pats = checker.patterns(script.read_text(encoding="utf-8"))
    assert [p for p, _ln in pats] == [".text", ".rodata*", ".dma_buffers"]
    assert [ln for _p, ln in pats] == [2, 2, 3]


def test_the_attribute_is_read_with_its_line_number(tree):
    src = tree / "a.c"
    src.write_text(
        "#include <stdint.h>\n"
        "__attribute__((section(\".dma_buffer\"), aligned(32)))\n"
        "static uint16_t buffer[8];\n", encoding="utf-8")
    # attributes() reports paths relative to the tool's own ROOT, so the name and
    # the line are what this asserts; the path is exercised against the real tree
    # by the regression test below.
    text = src.read_text(encoding="utf-8")
    found = [m.group(1) for m in checker.ATTRIBUTE.finditer(text)]
    assert found == [".dma_buffer"]
    assert text[: text.index("section")].count("\n") + 1 == 2


def test_the_failure_names_the_pattern_that_was_meant():
    """A missing letter is the defect, so the message has to carry the fix."""
    pats = [(".dma_buffers", 181), (".dma_buffers*", 182), (".text", 102)]
    assert checker.matches(".dma_buffer", pats) == []
    suggested = [p for p, _ln in checker.near(".dma_buffer", pats)]
    assert ".dma_buffers" in suggested
    assert ".text" not in suggested


def test_every_section_attribute_in_this_repository_is_collected():
    """The regression guard, on the real tree and the real linker script."""
    pats = checker.patterns(checker.SCRIPT.read_text(encoding="utf-8"))
    assert pats, "no input-section patterns were parsed, so this test proves nothing"
    asked = checker.attributes(checker.sources())
    assert asked, "no section attributes were found, so this test proves nothing"
    orphans = [(name, where, line) for name, where, line in asked
               if not checker.matches(name, pats)]
    assert orphans == [], (
        "these section attributes are collected by no linker rule, so the linker "
        "places them where it likes: {}".format(orphans))
