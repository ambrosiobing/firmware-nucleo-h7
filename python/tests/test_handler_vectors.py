"""check_handler_vectors: a defined interrupt handler with no vector is reported.

**The defect this pins, twice over.** On Wednesday 7 October 2026 P06's converter
bring-up reported every step ok, every register reading back, and no sample ever
arrived: `ADC_IRQHandler` was declared, compiled and linked, and was not in the vector
table, so the first end-of-conversion read a vector from past the end of that array
and branched into `.text`. On Friday 16 October 2026 the same thing was found again,
in P06's transfer-engine back end: a handler in the tree since Saturday 3 October 2026
with no DMA entry of any kind in the table. Three things must be true for a handler to
run, the peripheral's IER bit, the NVIC enable and a vector, and only the first two
read back.

**And one test here is about this checker's own first version, which under-reported.**
Its pattern began with `\\s*`, and `\\s` matches a newline, so a definition with a
blank line above it matched from the blank line, the line number came out one low, the
brace test looked at the handler's own line and the handler was DROPPED IN SILENCE.
`ADC_IRQHandler` has a blank line above it; `USART3_IRQHandler` has a comment. The
tool reported two handlers where the repository defines three and said nothing, which
is the failure a checker must never have, so the blank-line case is asserted here
rather than left to the repository happening to contain one.

`tempfile.mkdtemp` rather than pytest's `tmp_path`, whose `pytest-current` symlink
win11 aquamarine refuses.
"""
from __future__ import annotations

import pathlib
import shutil
import sys
import tempfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python" / "tools"))

import check_handler_vectors as checker  # noqa: E402


@pytest.fixture
def tree():
    d = pathlib.Path(tempfile.mkdtemp(prefix="handler-vectors-"))
    try:
        yield d
    finally:
        shutil.rmtree(d, ignore_errors=True)


def _definitions_of(text, tree):
    src = tree / "a.c"
    src.write_text(text, encoding="utf-8")
    return [name for name, _where, _line in checker.definitions([src])]


def test_a_definition_with_a_blank_line_above_it_is_found(tree):
    """The regression this checker's own first version failed."""
    text = ("static int x;\n"
            "\n"
            "void ADC_IRQHandler(void)\n"
            "{\n"
            "    x = 1;\n"
            "}\n")
    assert _definitions_of(text, tree) == ["ADC_IRQHandler"]


def test_a_definition_with_a_comment_above_it_is_found(tree):
    text = ("/* the console */\n"
            "void USART3_IRQHandler(void)\n"
            "{\n"
            "}\n")
    assert _definitions_of(text, tree) == ["USART3_IRQHandler"]


def test_a_declaration_is_not_a_definition(tree):
    """The weak aliases in startup.c end in a semicolon and must not count."""
    text = ("void TIM2_IRQHandler(void);\n"
            "void DMA1_Stream0_IRQHandler(void) __attribute__((weak));\n")
    assert _definitions_of(text, tree) == []


def test_the_body_may_be_separated_by_blank_lines(tree):
    text = ("void ADC_IRQHandler(void)\n"
            "\n"
            "{\n"
            "}\n")
    assert _definitions_of(text, tree) == ["ADC_IRQHandler"]


def test_the_macro_name_follows_the_table_s_convention():
    assert checker.wanted_macro("ADC_IRQHandler") == "IRQ_ADC"
    assert checker.wanted_macro("USART3_IRQHandler") == "IRQ_USART3"
    assert checker.wanted_macro("DMA1_Stream1_IRQHandler") == "IRQ_DMA1_STREAM1"


def test_the_table_entries_are_parsed_with_their_handler(tree):
    text = ("vectors[] = {\n"
            "    [IRQ_VECTOR_INDEX(IRQ_ADC)]    = ADC_IRQHandler,\n"
            "    [IRQ_VECTOR_INDEX(IRQ_DMA1_STREAM1)] = DMA1_Stream1_IRQHandler,\n"
            "};\n")
    got = {m.group(1): m.group(2) for m in checker.ENTRY.finditer(text)}
    assert got == {"IRQ_ADC": "ADC_IRQHandler",
                   "IRQ_DMA1_STREAM1": "DMA1_Stream1_IRQHandler"}


def test_every_handler_this_repository_defines_has_a_vector_naming_it():
    """The regression guard, on the real tree, the real table and the real header."""
    table = checker.STARTUP.read_text(encoding="utf-8")
    entries = {m.group(1): m.group(2) for m in checker.ENTRY.finditer(table)}
    assert entries, "no table entries were parsed, so this test proves nothing"
    handlers = checker.definitions(checker.sources())
    assert len(handlers) >= 3, (
        "only {} handlers were found and this repository defines at least three, "
        "so the pattern is under-reporting again: {}".format(len(handlers), handlers))
    missing = [(n, w, l) for n, w, l in handlers
               if entries.get(checker.wanted_macro(n)) != n]
    assert missing == [], (
        "these handlers are defined and no vector names them, so they compile, link "
        "and never run: {}".format(missing))


def test_every_designated_index_name_is_defined_in_the_register_header():
    """A designated index that does not compile is the same failure, earlier."""
    table = checker.STARTUP.read_text(encoding="utf-8")
    regs = checker.REGS.read_text(encoding="utf-8")
    used = {m.group(1) for m in checker.ENTRY.finditer(table)}
    bound = {m.group(1) for m in checker.DEFINE.finditer(regs)}
    assert used, "no index names were parsed, so this test proves nothing"
    assert used <= bound, "used as an index and defined nowhere: {}".format(
        sorted(used - bound))
