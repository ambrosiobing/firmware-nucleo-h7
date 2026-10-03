#!/usr/bin/env python3
"""Every project's state is written once, machine readable, and checked.

    python python/tools/check_status.py

What this is protecting. Until Saturday 3 October 2026 the state of each project
was prose in two places: a table in projects/README.md and a sentence in the
project's own README. They drifted. The table said P01, P02 and P09 had run on
the board and carried cycle counts measured there, while thirty lines below it
the same page still said that nothing in the repository had ever run on hardware
and that no figure anywhere was a measurement of anything physical. A reader who
finds that stops trusting the numbers, and rightly.

So each project README now carries one line, in one grammar:

    Status: c=board cpp=host python=none rust=none

and this script checks four things that prose cannot check itself:

  1. The line exists, exactly once, names all four languages, and uses only the
     four states: board, links, host, none.
  2. A language that claims a state other than none has at least one source file
     to back it, either in the project's own subdirectory for that language or at
     a path the project's own Markdown names and which exists.
  3. A language that claims none has no source file. A directory with code in it
     and a status of none is the same drift in the other direction.
  4. The strongest of a project's four states is a state the row for that project
     in projects/README.md actually claims, in that row's own words.

And one check that is not about status at all but which the same walk makes free:
every repository path a project's Markdown names must exist. The move to
per-project language directories broke three references to c/payload/payload.hpp,
a file that had never existed under that name, and no checker noticed because the
paths were inside backticks rather than in Markdown links.

Exit code 0 if everything agrees, 1 otherwise. Runs on any laptop: it reads files
and starts no compiler.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PROJECTS = ROOT / "projects"
TABLE = PROJECTS / "README.md"

LANGS = ("c", "cpp", "python", "rust")
STATES = ("board", "links", "host", "none")

# Strongest first, so the project's row can be checked against the strongest
# claim any of its four languages makes.
RANK = {"board": 3, "links": 2, "host": 1, "none": 0}

# What the table in projects/README.md may say for each state. More than one
# phrasing is allowed because the table is prose for a reader, not a database,
# but it may not say something that contradicts the key.
TABLE_PHRASES = {
    "board": ("runs on the board", "half done and measured"),
    "links": ("**links**",),
    "host": ("host only", "written and proven", "both gates written and proven"),
    "none": ("not started",),
}

SOURCE_SUFFIX = {
    "c": (".c", ".h"),
    "cpp": (".cpp", ".cc", ".hpp", ".hh"),
    "python": (".py",),
    "rust": (".rs",),
}

STATUS_RE = re.compile(r"^Status:\s*(.+?)\s*$", re.M)

# A path-like token: at least one slash, no spaces, ends in a file extension this
# repository actually keeps. Picked up from backticks, from Markdown links and
# from indented listings alike. Longest extensions first, or .cmake is read as
# .c and the path is reported as a file that was never named.
PATH_RE = re.compile(
    r"((?:[A-Za-z0-9_.-]+/)+[A-Za-z0-9_.-]+"
    r"\.(?:cmake|json|toml|hpp|cpp|yml|tex|ps1|md|ld|py|rs|c|h))"
    r"(?![A-Za-z0-9])")

# First segments that name something built rather than committed, or a directory
# on the board or at the bench rather than in this repository. MICROPYTHON.md
# describes the board's own layout as codec/ and test/, and a capture is written
# at the bench and never committed, so neither is a path this checker can resolve
# or should try to.
PATH_SKIP_ROOT = {"build", "build-host", "build-fw", ".venv", "Drivers", "CMSIS",
                  "Core", "usr", "opt", "home", "mnt", "dev",
                  "codec", "test", "witness", "tools", "captures"}

# Two paths this repository names on purpose in order to say they do not exist.
# Declared here with the reason rather than tolerated silently, so that a reader
# of this script learns the same thing a reader of those READMEs learns, and so
# that the day one of them is written the exception is reported as stale instead
# of hiding a file that now exists.
NAMED_AS_ABSENT = {
    "python/tools/reconcile_size.py":
        "the map file against size output reconciliation the chapter asks for; "
        "P01 says it waits for a build that produces either",
    "docs/results.md":
        "P03's measured rate for where loss begins; the README says it does not "
        "exist because inventing a plausible rate would be the worst outcome",
}

problems = []


def note(where, msg):
    problems.append("{}: {}".format(where, msg))


def project_dirs():
    return sorted(d for d in PROJECTS.iterdir()
                  if d.is_dir() and re.match(r"^P\d\d-", d.name))


def read_status(readme):
    """The one status line, parsed, or None with the problem already recorded."""
    text = readme.read_text(encoding="utf-8")
    found = STATUS_RE.findall(text)
    rel = readme.relative_to(ROOT).as_posix()
    if not found:
        note(rel, "no Status line. Expected one line of the form "
                  "Status: c=board cpp=none python=none rust=none")
        return None
    if len(found) > 1:
        note(rel, "{} Status lines, expected exactly one".format(len(found)))
        return None
    status = {}
    for token in found[0].split():
        if "=" not in token:
            note(rel, "Status token {!r} is not lang=state".format(token))
            return None
        lang, state = token.split("=", 1)
        if lang not in LANGS:
            note(rel, "Status names an unknown language {!r}".format(lang))
            return None
        if state not in STATES:
            note(rel, "Status gives {} the state {!r}, which is not one of {}"
                 .format(lang, state, ", ".join(STATES)))
            return None
        status[lang] = state
    missing = [lang for lang in LANGS if lang not in status]
    if missing:
        note(rel, "Status omits {}. All four languages are named, and a language "
                  "with nothing written is none rather than absent"
             .format(", ".join(missing)))
        return None
    return status


def sources_in(directory, lang):
    if not directory.is_dir():
        return []
    return [p for p in sorted(directory.iterdir())
            if p.is_file() and p.suffix in SOURCE_SUFFIX[lang]]


def named_paths(project):
    """Every repository path the project's own Markdown names, and whether it
    exists. Relative paths are resolved from the file that names them."""
    out = []
    for md in sorted(project.rglob("*.md")):
        text = md.read_text(encoding="utf-8")
        for raw in PATH_RE.findall(text):
            if raw.lstrip("./").split("/")[0] in PATH_SKIP_ROOT:
                continue
            if "..." in raw:          # a prose abbreviation is not a path
                continue
            for base in (md.parent, ROOT):
                candidate = (base / raw)
                try:
                    candidate = candidate.resolve()
                except OSError:
                    continue
                if candidate.exists():
                    out.append((md, raw, candidate))
                    break
            else:
                out.append((md, raw, None))
    return out


def main():
    projects = project_dirs()
    if len(projects) != 20:
        note("projects/", "{} project directories, expected 20"
             .format(len(projects)))

    table = TABLE.read_text(encoding="utf-8")
    checked = 0
    paths_checked = 0

    for project in projects:
        rel = project.relative_to(ROOT).as_posix()
        key = project.name.split("-")[0]
        readme = project / "README.md"
        if not readme.is_file():
            note(rel, "no README.md")
            continue

        # 4. the four directories, each with a README of its own.
        for lang in LANGS:
            d = project / lang
            if not d.is_dir():
                note(rel, "no {}/ directory. Every project carries all four, so "
                          "the four language routes are visible".format(lang))
            elif not (d / "README.md").is_file():
                note(rel + "/" + lang, "no README.md saying what this language "
                                       "carries for this project")

        status = read_status(readme)
        if status is None:
            continue
        checked += 1

        paths = named_paths(project)
        paths_checked += len(paths)
        for md, raw, resolved in paths:
            if raw in NAMED_AS_ABSENT:
                continue
            if resolved is None:
                note(md.relative_to(ROOT).as_posix(),
                     "names {}, which does not exist".format(raw))

        for lang in LANGS:
            state = status[lang]
            own = sources_in(project / lang, lang)
            if state == "none":
                # 3. a directory with code in it is not none.
                if own:
                    note(rel, "{} is none, but {}/ holds {}"
                         .format(lang, lang,
                                 ", ".join(p.name for p in own)))
            else:
                # 2. a claim needs a file behind it.
                if own:
                    continue
                backing = [raw for _, raw, resolved in paths
                           if resolved is not None
                           and raw.endswith(SOURCE_SUFFIX[lang])]
                if not backing:
                    note(rel, "{} claims {}, but {}/ is empty and no existing "
                              "source file for that language is named anywhere "
                              "in this project".format(lang, state, lang))

        # 1 and 4 done; 5: the table must not contradict the key.
        strongest = max(status.values(), key=lambda s: RANK[s])
        row = ""
        for line in table.splitlines():
            if line.startswith("| [" + key + "]") or line.startswith("| " + key + " "):
                row = line
                break
        if not row:
            note("projects/README.md", "no table row for {}".format(key))
            continue
        if not any(phrase in row for phrase in TABLE_PHRASES[strongest]):
            note("projects/README.md",
                 "{}'s key says {} is its strongest state, and its row says none "
                 "of {}".format(key, strongest,
                                " / ".join(TABLE_PHRASES[strongest])))

    for raw, reason in sorted(NAMED_AS_ABSENT.items()):
        if (ROOT / raw).exists():
            note("check_status.py",
                 "{} is listed as named-on-purpose-absent, and it now exists. "
                 "Remove the entry and the sentence that says it does not."
                 .format(raw))

    print("{} projects, {} with a readable status line, {} named paths resolved"
          .format(len(projects), checked, paths_checked))
    print("{} paths are named in order to say they do not exist: {}"
          .format(len(NAMED_AS_ABSENT), ", ".join(sorted(NAMED_AS_ABSENT))))
    if problems:
        print()
        for p in problems:
            print("  " + p)
        print()
        print("PROBLEMS: {}".format(len(problems)))
        return 1
    print("every project's state agrees with its code and with the table")
    return 0


if __name__ == "__main__":
    sys.exit(main())
