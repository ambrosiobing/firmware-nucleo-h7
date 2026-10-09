#!/usr/bin/env python3
"""Everything the workflow can no longer check, run here before you push.

    python prepublish.py

The authoring sources are not published. Only the Markdown edition is, and
`*.tex` is ignored, so continuous integration cannot see `sections/` or
`figures/*.tex` and cannot verify that the Markdown still matches them. Four
checks lost their input that way, and they are exactly the ones that prove the
book agrees with its source:

  * the house rules over every chapter source
  * the book-level consistency check across main.tex and the chapters
  * the per-chapter figure and project-line existence check
  * the regeneration check, that chapters/, CONTENTS.md and the figure SVGs are
    current rather than merely present

They did not stop mattering when the workflow stopped seeing them. They run
here instead, and this script refuses rather than warns, so that "I ran
prepublish" means what a green run used to mean.

The order matters. Regeneration runs first, because with the sources ignored,
editing one no longer shows in `git status`, so nothing reminds anybody to
rebuild. Everything after it reads what the regeneration produced.

One limit is reported rather than hidden. `mdbuild.py` does not draw figures, it
copies whatever `build/fig` already holds, so the drift check only covers the
figures that have been rendered. The figure coverage is printed every run, and a
partial build says so out loud instead of passing quietly. `python build.py
--figures` is what fills that directory.

This script is self-contained. It reads nothing outside this repository and
depends on no other volume.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent

CHAPTERS = 20
PREFIX = "c"                                        # sections/cNN.tex
FIGURES = ("arch", "wiring", "uml", "mem", "timing")
GENERATED = ["chapters", "CONTENTS.md", "figures"]  # figures holds the SVGs
FIGBUILD = ROOT / "build" / "fig"


def run(label, args):
    print(f"\n=== {label}")
    done = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    out = (done.stdout + done.stderr).strip()
    if out:
        print("\n".join(out.splitlines()[-12:]))
    if done.returncode != 0:
        print(f"FAILED: {label}")
        return False
    return True


def sources_present():
    """The sources are untracked now, so their absence is a real possibility."""
    missing = [p for p in ("sections", "figures", "main.tex", "tikz_preamble.tex")
               if not (ROOT / p).exists()]
    if missing:
        print(f"\nThe authoring sources are not in this clone: {', '.join(missing)}.")
        print("They are deliberately unpublished, so a fresh clone does not have")
        print("them. Bring them in from the authoring copy before publishing.")
        return False
    return True


def regenerated_cleanly():
    """What mdbuild writes must not move when it is run again."""
    done = subprocess.run(["git", "diff", "--quiet", "--"] + GENERATED, cwd=ROOT)
    if done.returncode != 0:
        print("\nA generated file moved when it was regenerated, which means what is")
        print("committed is behind the sources. Commit the regeneration:")
        subprocess.run(["git", "--no-pager", "diff", "--stat", "--"] + GENERATED,
                       cwd=ROOT)
        return False
    return True


def figure_coverage():
    """How much of the figure drift check is real, stated as a number.

    mdbuild copies from build/fig rather than drawing, so a figure whose source
    changed and was never rendered is invisible to the drift check above. That
    is a gap in the checking, not in the book, and a gap nobody can see is the
    failure this volume keeps finding. So it is counted and printed.
    """
    sources = {p.stem for p in (ROOT / "figures").glob("*.tex")}
    built = {p.stem for p in FIGBUILD.glob("*.svg")} if FIGBUILD.exists() else set()
    missing = sorted(sources - built)
    print(f"  {len(sources) - len(missing)} of {len(sources)} figures are rendered "
          f"in build/fig and were compared")
    if missing:
        print(f"  {len(missing)} were not, so their SVGs went unchecked against their")
        print("  sources. Run python build.py --figures to close the gap, or accept")
        print("  it knowingly. First few: " + ", ".join(missing[:6]))
    return missing


def chapters_complete():
    """Each chapter needs its source, its project line and its five figures."""
    problems = []
    for n in range(1, CHAPTERS + 1):
        stem = f"{PREFIX}{n:02d}"
        sec = ROOT / "sections" / f"{stem}.tex"
        if not sec.exists():
            problems.append(f"missing sections/{stem}.tex")
            continue
        if "project{" not in sec.read_text(encoding="utf-8"):
            problems.append(f"{stem}: no project line")
        for fig in FIGURES:
            if not (ROOT / "figures" / f"{stem}_{fig}.tex").exists():
                problems.append(f"missing figures/{stem}_{fig}.tex")
    for extra in ("sections/front.tex", "sections/appendix.tex",
                  "figures/front_map.tex"):
        if not (ROOT / extra).exists():
            problems.append(f"missing {extra}")
    for p in problems:
        print("  " + p)
    return not problems


def no_source_committed():
    """The rule this whole arrangement exists for, checked here as well as in CI."""
    done = subprocess.run(["git", "ls-files"], cwd=ROOT,
                          capture_output=True, text=True)
    tex = [l for l in done.stdout.splitlines() if l.endswith(".tex")]
    if tex:
        print("\nThese authoring sources are committed and must not be:")
        for t in tex[:10]:
            print("  " + t)
        if len(tex) > 10:
            print(f"  ... and {len(tex) - 10} more")
        return False
    return True


def main():
    if not sources_present():
        return 2

    ok = True
    ok &= run("Regenerating the Markdown edition", [sys.executable, "mdbuild.py"])
    print("\n=== The generated files are current")
    ok &= regenerated_cleanly()
    print("\n=== How much of the figure check is real")
    unrendered = figure_coverage()
    print("\n=== Every chapter has its five figures and a project line")
    ok &= chapters_complete()
    print("\n=== No authoring source is committed")
    ok &= no_source_committed()
    ok &= run("House rules over every chapter", [sys.executable, "lint.py"])
    ok &= run("Book-level consistency", [sys.executable, "crosscheck.py"])

    print()
    if unrendered:
        print(f"prepublish: WARNING. {len(unrendered)} figures were not rendered, so")
        print("their published SVGs were not compared against their sources. Run")
        print("python build.py --figures on a machine with LaTeX before a release,")
        print("or accept the gap knowingly rather than by default.")
    if ok:
        print("prepublish: all checks that could run passed."
              if unrendered else "prepublish: all checks passed.")
        print("Safe to commit and push.")
        return 0
    print("prepublish: FAILED. Do not push until the above is fixed.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
