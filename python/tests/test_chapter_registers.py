"""check_chapter_registers: a register a build writes that its chapter never names.

**The defects this pins.** Four, all in chapter six and all found on Tuesday
20 October 2026 by one question asked mechanically: which registers does a project's
C write whose spelling its chapter never contains? The chapter brought the converter
up nowhere, so all three of its builds would have reported correct configuration and
produced no samples. It toggled a marker pin it never clocked or configured, and
that pin is the chapter's whole instrument, so the witness would have shown a flat
line. It asked for an overrun count and never enabled the overrun interrupt. And the
step written that morning to fix the first of those began at deep power-down, leaving
out the input pin that the file it was drawn from puts first.

**Why the fourth one is the reason this file exists.** It was found by re-running the
comparison after acting on its output, not by reading. A candidate list is a snapshot
of the state before whatever was done about it, and a human reading the names left
over will conclude they are spelling differences. A checker re-runs by construction.

**What the tests assert.** Both directions first, since a check that has never failed
is indistinguishable from one that cannot: an undocumented write is reported and a
documented one is not. Then the allowlist, in both of its failure modes, because an
exemption that has outlived its reason is how an allowlist becomes a blindfold: an
entry for a register the code stopped writing is stale, and so is one the chapter has
since come to name. Then that every one of the twelve live exemptions is justified
against the tree as it is today, which is the test that will fail when somebody
deletes a register write or documents one. Then that comments are not writes, since
this volume's C is mostly comment.

**Then a write through an array member, which this checker could not see at all
until Sunday 25 October 2026.** `GPIOB->AFR[0] = 2u` is a register write, and the
pattern wanted the `=` to follow the register name with only whitespace and an
optional compound operator in between, so a subscript made the whole thing match
nothing: the register appeared in no output, as neither documented nor undocumented.
Nothing had slipped through, and that is checked rather than assumed: the tree held
no write of this shape when the pattern was widened. Four tests cover it, and one of
the four is about the widening rather than the hole, because a pattern that allows a
compound operator one character after a subscript could easily start reporting
`==`, `!=`, `<=` and `>=` as writes, which would make every read of a register a
finding.

Then the known blind spot, asserted on purpose so it cannot change silently. Then
the regression guard, that the repository today has nothing undocumented and nothing
stale.

`tempfile.mkdtemp` rather than pytest's `tmp_path`, whose `pytest-current` symlink
win11 aquamarine refuses, as `test_rate_parity.py` has recorded since Sunday
4 October 2026.
"""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python" / "tools"))

import check_chapter_registers as chk          # noqa: E402

PROJECT = "P06-timer-sampling"


# ----------------------------------------------------------- both directions

def test_an_undocumented_write_is_reported():
    code = "void f(void) { ADC1->CFGR = 7u; }"
    unexplained, stale = chk.compare(PROJECT, code, "a chapter with no registers",
                                     allowed={})
    assert unexplained == ["ADC1->CFGR"]
    assert stale == []


def test_a_write_the_chapter_names_is_not_reported():
    code = "void f(void) { ADC1->CFGR = 7u; }"
    chapter = "Set the data management field in `ADC1->CFGR` before arming."
    unexplained, stale = chk.compare(PROJECT, code, chapter, allowed={})
    assert unexplained == []
    assert stale == []


def test_a_compound_assignment_is_a_write():
    code = "void f(void) { ADC1->IER |= ADC_IER_OVRIE; }"
    unexplained, _ = chk.compare(PROJECT, code, "no registers here", allowed={})
    assert unexplained == ["ADC1->IER"]


def test_a_comparison_is_not_a_write():
    code = "void f(void) { if (ADC1->ISR == 0u) { return; } }"
    unexplained, _ = chk.compare(PROJECT, code, "no registers here", allowed={})
    assert unexplained == []


def test_a_write_inside_a_comment_is_not_a_write():
    code = "/* once this said ADC1->CFGR = 7u; and it no longer does */\nvoid f(void) { }"
    unexplained, _ = chk.compare(PROJECT, code, "no registers here", allowed={})
    assert unexplained == []


# ----------------------------------------------------------------- allowlist

def test_an_exempted_write_is_not_reported():
    code = "void f(void) { TIM6->CNT = 0u; }"
    allowed = {(PROJECT, "TIM6->CNT"): "reset value is zero"}
    unexplained, stale = chk.compare(PROJECT, code, "nothing", allowed=allowed)
    assert unexplained == []
    assert stale == []


def test_an_exemption_for_another_project_does_not_apply():
    code = "void f(void) { TIM6->CNT = 0u; }"
    allowed = {("P02-ring-buffer", "TIM6->CNT"): "a different project"}
    unexplained, _ = chk.compare(PROJECT, code, "nothing", allowed=allowed)
    assert unexplained == ["TIM6->CNT"]


def test_an_exemption_the_code_dropped_is_stale():
    allowed = {(PROJECT, "TIM6->CNT"): "reset value is zero"}
    unexplained, stale = chk.compare(PROJECT, "void f(void) { }", "nothing",
                                     allowed=allowed)
    assert unexplained == []
    assert stale == [("TIM6->CNT", "the code no longer writes it")]


def test_an_exemption_the_chapter_now_names_is_stale():
    code = "void f(void) { TIM6->CNT = 0u; }"
    chapter = "The sketch zeroes `TIM6->CNT` before enabling the timer."
    allowed = {(PROJECT, "TIM6->CNT"): "reset value is zero"}
    unexplained, stale = chk.compare(PROJECT, code, chapter, allowed=allowed)
    assert unexplained == []
    assert stale == [("TIM6->CNT", "the chapter names it now")]


def test_every_live_exemption_is_justified_against_the_tree():
    """Each entry must name a project that exists and a register it writes.

    This is the test that fails when a register write is deleted or a chapter
    starts documenting one, which is the point: the exemption should go with it.
    """
    by_project = {}
    for proj, chapter in chk.pairs():
        code = "\n".join(chk.read(p) for p in sorted((proj / "c").glob("*.c")))
        by_project[proj.name] = (chk.written(code), chk.mentioned(chk.read(chapter)))

    assert chk.ALLOWED, "an empty allowlist means the exemptions were lost"
    for (project, name), reason in sorted(chk.ALLOWED.items()):
        assert project in by_project, "%s names no project with C" % project
        writes, mentions = by_project[project]
        assert name in writes, "%s does not write %s any more" % (project, name)
        assert name not in mentions, "%s names %s now" % (project, name)
        assert reason and reason == reason.strip(), "%s has no reason" % name
        assert len(reason) > 20, "%s's reason is too short to be one" % name
        assert "TODO" not in reason, "%s's reason is a placeholder" % name


# ------------------------------------------- a write through an array member

def test_a_write_through_an_array_member_is_seen():
    """GPIOB->AFR[0] = 2u is a register write and was invisible until today.

    Sunday 25 October 2026. The pattern required the `=` to follow the register
    name with only whitespace and an optional compound operator between them, so
    a subscript in between matched nothing: the register did not appear in the
    output in any form, undocumented or otherwise. Nothing had slipped through,
    because the tree held no write of this shape when the pattern was widened.
    """
    code = "void f(void) { MARKER_PORT->AFR[0] = 2u; }"
    unexplained, _ = chk.compare(PROJECT, code, "a chapter with no registers",
                                 allowed={})
    assert unexplained == ["MARKER_PORT->AFR"]


def test_an_array_member_write_the_chapter_names_is_not_reported():
    code = "void f(void) { MARKER_PORT->AFR[0] = 2u; }"
    chapter = "Select alternate function 2 in `MARKER_PORT->AFR[0]` for the pin."
    unexplained, stale = chk.compare(PROJECT, code, chapter, allowed={})
    assert unexplained == []
    assert stale == []


def test_a_subscripted_comparison_is_not_a_write():
    """The widened pattern must not start counting comparisons as writes.

    Three operators put an `=` one character after the subscript and none of
    them is an assignment. Each is checked rather than one standing for all,
    because the pattern allows a compound operator in exactly that position and
    a mistake there would report every read of a register as a write.
    """
    for expr in ("if (MARKER_PORT->AFR[0] == 2u)",
                 "if (MARKER_PORT->AFR[0] != 2u)",
                 "if (MARKER_PORT->AFR[0] <= 2u)",
                 "if (MARKER_PORT->AFR[0] >= 2u)"):
        code = "void f(void) { %s { } }" % expr
        unexplained, _ = chk.compare(PROJECT, code, "nothing", allowed={})
        assert unexplained == [], expr


def test_a_compound_assignment_through_an_array_member_is_seen():
    code = "void f(void) { MARKER_PORT->AFR[0] |= (2u << 16); }"
    unexplained, _ = chk.compare(PROJECT, code, "nothing", allowed={})
    assert unexplained == ["MARKER_PORT->AFR"]


# ------------------------------------------------------- the known blind spot

def test_a_write_through_a_pointer_is_invisible_and_that_is_recorded():
    """Asserted on purpose, so the limitation cannot change without a red run.

    A driver that passes a GPIO_TypeDef * around writes registers this checker
    cannot see. The header says so; this pins it.
    """
    code = "void f(GPIO_TypeDef *port) { port->MODER = 0u; }"
    unexplained, _ = chk.compare(PROJECT, code, "nothing", allowed={})
    assert unexplained == [], "the blind spot closed: update the header and this test"


# ---------------------------------------------------------- regression guard

def test_the_repository_today_has_nothing_undocumented_and_nothing_stale():
    total_unexplained, total_stale = [], []
    for proj, chapter in chk.pairs():
        code = "\n".join(chk.read(p) for p in sorted((proj / "c").glob("*.c")))
        unexplained, stale = chk.compare(proj.name, code, chk.read(chapter))
        total_unexplained += [(proj.name, n) for n in unexplained]
        total_stale += [(proj.name, n, w) for n, w in stale]
    assert total_unexplained == [], total_unexplained
    assert total_stale == [], total_stale


def test_every_project_with_c_is_paired_with_its_chapter():
    found = chk.pairs()
    assert found, "no project with C was paired with a chapter"
    for proj, chapter in found:
        assert proj.name[1:3] == chapter.name[:2], (proj.name, chapter.name)
        assert sorted((proj / "c").glob("*.c")), proj.name
