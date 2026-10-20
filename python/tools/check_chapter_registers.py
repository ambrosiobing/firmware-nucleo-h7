#!/usr/bin/env python3
"""Every register a project's code writes must appear in its chapter, or be exempt.

WHY THIS EXISTS. On Tuesday 20 October 2026 this comparison was run once, by hand,
as a scratchpad script, and it found four things in chapter six that a reader could
not have recovered from.

The chapter brought the converter up nowhere. It mentioned none of DEEPPWD,
ADVREGEN, ADCAL, ADEN, ADRDY, BOOST, PCSEL, SMPR or SQR1, and not the common
register's clock field, and all three of its acquisition sketches went straight to
ADSTART. A reader following it would have built three back ends, watched every one
report correct configuration, and got samples from none.

It toggled a marker pin it never clocked or configured, writing BSRR and nothing
else. That pin is the chapter's entire instrument, since every criterion except the
console readings is measured from its edges, so the failure would have presented as
a flat line on the witness with all three builds apparently correct.

It asked for an overrun count in its acceptance criterion and never enabled the
overrun interrupt.

And the step written that morning to fix the first of those began at deep
power-down, leaving out the input pin that adc_bringup.c puts first under a comment
saying every register below it can be correct while nothing reaches the converter,
silently. That one was found by re-running the comparison rather than by trusting
that the names still in its output were spelling differences.

WHY IT IS A CHECKER RATHER THAN A SCRIPT SOMEBODY REMEMBERS. The same standard that
put check_doc_commands.py in the suite: a check that has already found real errors
belongs where it cannot stop running. The allowlist is the other half of that. Every
name this comparison reports and does not fail on is written down with the reason it
is acceptable, so an exemption is a decision somebody made rather than a silence,
and a NEW register write must either appear in its chapter or be exempted
deliberately.

WHAT IT CHECKS. For each chapter NN and project PNN that has C, every PERIPH->REG
spelling that appears on the left of an assignment in the project's C, against every
PERIPH->REG spelling that appears anywhere in the chapter. A write whose spelling is
absent from the chapter and absent from ALLOWED is a finding.

It also reports a STALE exemption: an entry in ALLOWED for a register the code no
longer writes, or one the chapter has since come to mention. An exemption that has
outlived its reason is how an allowlist turns into a blindfold.

WHAT IT CANNOT DO, and each of these is a real hole rather than a formality.

It matches a spelling, so a write through a helper function, a pointer parameter or
a vendor macro is invisible to it. marker_init's writes are found only because that
file names the port in a macro that expands to a spelling of this shape; a driver
that passed GPIO_TypeDef * around would hide every one of them.

It says nothing about order, masking or value, which is where most of this volume's
defects have been: DMNGT was found because the chapter never named the register at
all, and a chapter that named it and gave the wrong field would pass here.

It compares against the WHOLE chapter, not against the step that should carry the
write, so a register mentioned once in a sentence about something else counts as
covered.

It is blind to the reverse direction. A chapter that documents a register no build
writes is not reported, because that is ordinary in a volume where twelve projects
have no code and chapters describe work that has not started.

And it cannot tell a teaching sketch from a specification. Chapter six numbers its
stream 0 where the implementation uses stream 1, deliberately, and five of the
eleven exemptions below exist for that one choice.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# A write is a PERIPH->REG on the left of an assignment, including compound
# assignment. The trailing [^=] keeps `==` out.
WRITE = re.compile(r"\b([A-Z][A-Za-z0-9_]*)->([A-Z][A-Za-z0-9_]*)"
                   r"\s*(?:\||&|\^|\+|-)?=[^=]")
MENTION = re.compile(r"\b([A-Z][A-Za-z0-9_]*)->([A-Z][A-Za-z0-9_]*)")

# Every name this comparison reports and does not fail on, with the reason.
# A new entry is a decision: it says a reader of the chapter does not need this
# register named, and says why. Removing one is how a chapter earns its coverage.
ALLOWED = {
    # The chapter's sketch numbers the transfer engine's stream 0 and the
    # implementation moved to stream 1 on Friday 16 October 2026, when the
    # handler turned out to have no vector. The chapter teaches the mechanism
    # and the project owns which stream carries it.
    ("P06-timer-sampling", "DMA1_Stream1->CR"):
        "the chapter's sketch is stream 0; the build uses stream 1",
    ("P06-timer-sampling", "DMA1_Stream1->M0AR"):
        "the chapter's sketch is stream 0; the build uses stream 1",
    ("P06-timer-sampling", "DMA1_Stream1->M1AR"):
        "the chapter's sketch is stream 0; the build uses stream 1",
    ("P06-timer-sampling", "DMA1_Stream1->NDTR"):
        "the chapter's sketch is stream 0; the build uses stream 1",
    ("P06-timer-sampling", "DMA1_Stream1->PAR"):
        "the chapter's sketch is stream 0; the build uses stream 1",
    # The chapter calls the marker's port MARKER_PORT and the code's macro is
    # MARKER_GPIO. Since Tuesday 20 October 2026 both configure the same five
    # registers, so this is a macro name and not a missing write.
    ("P06-timer-sampling", "MARKER_GPIO->BSRR"):
        "the chapter spells the same port MARKER_PORT",
    ("P06-timer-sampling", "MARKER_GPIO->MODER"):
        "the chapter spells the same port MARKER_PORT",
    ("P06-timer-sampling", "MARKER_GPIO->OSPEEDR"):
        "the chapter spells the same port MARKER_PORT",
    ("P06-timer-sampling", "MARKER_GPIO->OTYPER"):
        "the chapter spells the same port MARKER_PORT",
    ("P06-timer-sampling", "MARKER_GPIO->PUPDR"):
        "the chapter spells the same port MARKER_PORT",
    # tim6_bringup.c writes CR1, PSC, ARR, CNT and EGR in that order and the
    # chapter's sketch omits the counter alone. Its reset value is zero and the
    # sketch runs from reset, so the two reach the same state; the explicit
    # write is there because the bring-up is called again without a reset when
    # a back end restarts.
    ("P06-timer-sampling", "TIM6->CNT"):
        "reset value is zero and the chapter's sketch starts from reset",
}


def code_only(text):
    """The text with comment bodies removed and the line structure kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text[i:i + 2] == "/*":
            end = text.find("*/", i + 2)
            if end == -1:
                out.append("\n" * text.count("\n", i))
                break
            out.append("\n" * text.count("\n", i, end))
            i = end + 2
            continue
        if text[i:i + 2] == "//":
            end = text.find("\n", i)
            if end == -1:
                break
            i = end
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def written(code_text):
    """Every PERIPH->REG written in this C, comments excluded."""
    body = code_only(code_text)
    return set("%s->%s" % (m.group(1), m.group(2)) for m in WRITE.finditer(body))


def mentioned(chapter_text):
    """Every PERIPH->REG the chapter contains anywhere, prose included."""
    return set("%s->%s" % (m.group(1), m.group(2))
               for m in MENTION.finditer(chapter_text))


def compare(project, code_text, chapter_text, allowed=None):
    """(unexplained, stale) for one project against its chapter.

    `unexplained` is written, not in the chapter, not exempt: a finding.
    `stale` is an exemption that no longer applies, because the code stopped
    writing the register or the chapter came to name it.
    """
    allowed = ALLOWED if allowed is None else allowed
    w, m = written(code_text), mentioned(chapter_text)
    exempt = set(name for (proj, name) in allowed if proj == project)
    unexplained = sorted(w - m - exempt)
    stale = sorted((name, "the code no longer writes it") for name in exempt
                   if name not in w)
    stale += sorted((name, "the chapter names it now") for name in exempt
                    if name in w and name in m)
    return unexplained, sorted(stale)


def pairs():
    """(project directory, chapter file) for every project whose number has both."""
    out = []
    for proj in sorted((ROOT / "projects").glob("P[0-9][0-9]-*")):
        number = proj.name[1:3]
        chapter = sorted((ROOT / "chapters").glob("%s-*.md" % number))
        if not chapter:
            continue
        if not sorted((proj / "c").glob("*.c")):
            continue
        out.append((proj, chapter[0]))
    return out


def read(path):
    return path.read_bytes().decode("utf-8", "replace").replace("\r\n", "\n")


def main():
    findings, stales, checked, projects = [], [], 0, 0
    for proj, chapter in pairs():
        code = "\n".join(read(p) for p in sorted((proj / "c").glob("*.c")))
        unexplained, stale = compare(proj.name, code, read(chapter))
        checked += len(written(code))
        projects += 1
        for name in unexplained:
            findings.append((proj.name, chapter.name, name))
        for name, why in stale:
            stales.append((proj.name, name, why))

    for project, chapter, name in findings:
        print("  UNDOCUMENTED  %s writes %s, absent from %s"
              % (project, name, chapter))
    for project, name, why in stales:
        print("  STALE EXEMPTION  %s %s: %s" % (project, name, why))

    print("\n  %d projects with C, %d register writes checked, %d exemptions,"
          " %d undocumented, %d stale"
          % (projects, checked, len(ALLOWED), len(findings), len(stales)))

    if findings or stales:
        print("\n  A register a build writes and its chapter never names is a")
        print("  reader following the chapter and getting a board that reports")
        print("  itself correct and does nothing. Name it in the chapter, or add")
        print("  it to ALLOWED with the reason a reader does not need it. A stale")
        print("  exemption is removed rather than kept: an allowlist that outlives")
        print("  its reasons is a blindfold.")
        return 1

    print("\n  Every register these builds write is named in its chapter or")
    print("  exempted with a reason. That does NOT mean the chapters are right:")
    print("  this matches a spelling, not an order, a mask or a value, and a")
    print("  write through a helper or a pointer is invisible to it. See this")
    print("  file's header for the full list of what it cannot do.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
