#!/usr/bin/env python3
"""Every relative link in every Markdown file must resolve.

    python tools/check_links.py

This exists because of a defect that reached the published repository on
Thursday 1 October 2026. `projects/README.md` is the code index a visitor lands
on, and all twenty of its project links read `](projects/P01-.../)` from inside
`projects/`, so every one was dead. The file had been the repository root's
README before the book and the code were merged, where the prefix was right, and
moving it down one level invalidated all twenty at once.

Nothing caught it. The linter checks prose and code blocks, crosscheck checks
figures and cross-references, and neither looks at a link. A broken link is the
first thing a reader meets and the last thing anybody tests, so it is checked
here, on every push, over every Markdown file in the repository.

What is checked:
  - relative links resolve to a file or directory that exists
  - in-page anchors point at a heading that exists in that same file
  - external links are listed and not fetched, because a check that needs the
    network fails for reasons that have nothing to do with this repository
"""
from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent.parent   # python/tools -> repo root
SKIP_DIRS = {".git", ".venv", "build", "build-host", "__pycache__", ".pytest_cache"}

# [text](target) and [ref]: target, which is the form the project READMEs use.
INLINE = re.compile(r"\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
REFERENCE = re.compile(r"^\s*\[[^\]]+\]:\s*<?(\S+)>?\s*$", re.MULTILINE)


def markdown_files() -> list[Path]:
    out = []
    for path in ROOT.rglob("*.md"):
        if any(part in SKIP_DIRS for part in path.relative_to(ROOT).parts):
            continue
        out.append(path)
    return sorted(out)


def headings(text: str) -> set[str]:
    """GitHub's anchor form: lowercase, spaces to hyphens, punctuation dropped."""
    anchors = set()
    for line in text.splitlines():
        if not line.startswith("#"):
            continue
        title = line.lstrip("#").strip()
        slug = re.sub(r"[^\w\- ]", "", title.lower()).replace(" ", "-")
        anchors.add(slug)
    return anchors


def main() -> int:
    dead: list[str] = []
    bad_anchor: list[str] = []
    external = 0
    checked = 0

    for md in markdown_files():
        text = md.read_text(encoding="utf-8")
        own_anchors = headings(text)
        targets = [m.group(1) for m in INLINE.finditer(text)]
        targets += [m.group(1) for m in REFERENCE.finditer(text)]

        for raw in targets:
            target = unquote(raw.strip())
            if target.startswith(("http://", "https://", "mailto:", "tel:")):
                external += 1
                continue
            checked += 1

            path_part, _, anchor = target.partition("#")
            rel = md.relative_to(ROOT).as_posix()

            if not path_part:
                # A pure in-page anchor.
                if anchor and anchor.lower() not in own_anchors:
                    bad_anchor.append("{}  #{}".format(rel, anchor))
                continue

            resolved = (md.parent / path_part).resolve()
            if not resolved.exists():
                dead.append("{}  ->  {}".format(rel, target))
                continue

            if anchor and resolved.suffix == ".md":
                other = headings(resolved.read_text(encoding="utf-8"))
                if anchor.lower() not in other:
                    bad_anchor.append("{}  ->  {}#{}".format(rel, path_part, anchor))

    print("{} Markdown files, {} relative links checked, {} external links left alone".format(
        len(markdown_files()), checked, external))

    if dead:
        print("\n{} DEAD LINK(S): the target does not exist".format(len(dead)))
        for d in dead:
            print("  " + d)
    if bad_anchor:
        print("\n{} BROKEN ANCHOR(S): no heading of that name".format(len(bad_anchor)))
        for b in bad_anchor:
            print("  " + b)

    if dead or bad_anchor:
        print("\nA broken link is the first thing a reader meets. Fix the target or the link.")
        return 1

    print("every relative link resolves and every anchor has a heading")
    return 0


if __name__ == "__main__":
    sys.exit(main())
