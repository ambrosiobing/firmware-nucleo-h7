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


def test_a_rule_that_exists_only_in_a_comment_is_not_a_rule(tree):
    """The self-matching failure, in the direction that would let a defect pass. A
    comment mentioning `*(.ghost)` must not make `.ghost` collected; the same text
    outside a comment must."""
    script = tree / "ghost.ld"
    script.write_text(
        "SECTIONS {\n"
        "  /* Put it FIRST, before *(.ghost), because order matters here. */\n"
        "  .text : { *(.text) }\n"
        "}\n", encoding="utf-8")
    pats = [pat for pat, _ln in checker.patterns(script.read_text(encoding="utf-8"))]
    assert pats == [".text"], pats

    script.write_text(
        "SECTIONS {\n"
        "  .ghost : { *(.ghost) }\n"
        "  .text : { *(.text) }\n"
        "}\n", encoding="utf-8")
    pats = [pat for pat, _ln in checker.patterns(script.read_text(encoding="utf-8"))]
    assert pats == [".ghost", ".text"], pats


def test_an_attribute_named_only_in_a_comment_is_not_an_attribute(tree):
    """The same failure in the other direction, where it would cry wolf: a comment
    explaining the `.dma_buffer` defect must not be reported as a defect."""
    src = tree / "b.c"
    src.write_text(
        "/* The attribute used to read section(\".dma_buffer\"), one letter short. */\n"
        "__attribute__((section(\".dma_buffers\")))\n"
        "static unsigned short buffer[8];\n", encoding="utf-8")
    text = checker.code_only(src.read_text(encoding="utf-8"))
    assert [m.group(1) for m in checker.ATTRIBUTE.finditer(text)] == [".dma_buffers"]


def test_stripping_a_comment_keeps_every_later_line_number(tree):
    """Line numbers are what makes a report actionable, so the blanking preserves
    them rather than closing the gap."""
    script = tree / "lines.ld"
    script.write_text(
        "SECTIONS {\n"
        "  /* one\n"
        "   * two\n"
        "   * three *(.decoy)\n"
        "   */\n"
        "  .text : { *(.text) }\n"
        "}\n", encoding="utf-8")
    pats = checker.patterns(script.read_text(encoding="utf-8"))
    assert pats == [(".text", 6)], pats


def test_a_comment_opener_inside_a_string_is_not_a_comment(tree):
    """Stepping over literals matters because the needle itself is a literal."""
    src = tree / "c.c"
    src.write_text(
        "static const char *u = \"https://example.invalid/*not a comment*/\";\n"
        "__attribute__((section(\".kept\")))\n"
        "static unsigned short kept[2];\n", encoding="utf-8")
    text = checker.code_only(src.read_text(encoding="utf-8"))
    assert [m.group(1) for m in checker.ATTRIBUTE.finditer(text)] == [".kept"]


def test_the_real_linker_script_has_one_rule_only_in_prose():
    """The instance that prompted the stripping, pinned so the fix cannot silently
    regress: the raw text yields one more rule than the code does, and it is
    `.text` from the comment above the P09 pad rule."""
    raw = checker.SCRIPT.read_text(encoding="utf-8")
    with_prose = [pat for pat, _ln in checker.patterns(
        raw.replace("/*", "   ").replace("*/", "   "))]
    code = [pat for pat, _ln in checker.patterns(raw)]
    assert len(with_prose) == len(code) + 1
    extra = list(with_prose)
    for pat in code:
        extra.remove(pat)
    assert extra == [".text"], extra


def test_the_line_number_survives_the_whole_attributes_path(tree, monkeypatch):
    """The path the report actually uses, which the test above deliberately does
    not exercise. Blanking the comments shortens the text in characters while
    keeping its lines, so a line number counted in a different string than the one
    searched comes out too small by every comment above it."""
    src = tree / "d.c"
    src.write_text(
        "/* A header comment with plenty of words in it, and more than one\n"
        " * line of them, so that a miscounted offset cannot pass unnoticed.\n"
        " * It also mentions section(\".decoy\") to earn its place here.\n"
        " */\n"
        "#include <stdint.h>\n"
        "\n"
        "__attribute__((section(\".real\"), aligned(32)))\n"
        "static uint16_t buffer[8];\n", encoding="utf-8")
    monkeypatch.setattr(checker, "ROOT", tree)
    assert checker.attributes([src]) == [(".real", "d.c", 7)]
