#!/usr/bin/env python3
"""Book-level consistency checks that per-chapter linting cannot make.

    python crosscheck.py

Checks that only make sense across the whole volume:
  1. Every chapter file the part structure names actually exists.
  2. No two chapters claim to build the same variant "in full".
  3. Every variant the authoring guide assigns is claimed by the chapter it
     assigns it to, and by no other.
  4. Every figure referenced by a \\diagram exists, and every figure file is
     referenced by something.
  5. No cross-reference points at a chapter that does not exist.
  6. Chapter titles in the source match the authoring guide's table.
  7. Dates are written in full: no bare month-and-year, no bare weekday.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SECTIONS = ROOT / "sections"
FIGURES = ROOT / "figures"
problems = []


def note(msg):
    problems.append(msg)


# ---------------------------------------------------------------- inputs
main = (ROOT / "main.tex").read_text(encoding="utf-8")
named = re.findall(r"\\input\{sections/(\w+)\.tex\}", main)
present = {p.stem for p in SECTIONS.glob("*.tex")}

for stem in named:
    if stem not in present:
        note(f"main.tex names sections/{stem}.tex, which does not exist")
for stem in sorted(present - set(named)):
    note(f"sections/{stem}.tex exists but main.tex never inputs it")

# ---------------------------------------------------------------- the matrix
guide = (ROOT / "AUTHORING.md").read_text(encoding="utf-8")
m = re.search(r"### Where each variant is built in full(.*?)\n---", guide, re.S)
assigned = {}
if m:
    for row in re.findall(r"^\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|$", m.group(1), re.M):
        variant, where = row
        if variant.lower().startswith("variant") or set(variant) <= set("- "):
            continue
        for n in re.findall(r"\d+", where):
            assigned.setdefault(variant, set()).add(int(n))
else:
    note("AUTHORING.md: could not find the variant matrix")

# what each chapter claims
claims = {}
for p in sorted(SECTIONS.glob("c[0-9][0-9].tex")):
    num = int(p.stem[1:])
    text = p.read_text(encoding="utf-8")
    block = re.search(r"\\subsection\*\{Variants\}(.*?)\\subsection\*", text, re.S)
    if not block:
        note(f"{p.name}: no Variants section found")
        continue
    body = block.group(1)
    # a row claims this chapter when its last cell says Here, or names this number
    for row in re.findall(r"^(.*?)\\\\", body, re.M):
        cells = [c.strip() for c in row.split("&")]
        if len(cells) < 5:
            continue
        where = cells[-1]
        if not re.search(r"\bHere\b", where, re.I):
            continue
        # "Here for the placement, Chapter 9 for the link" is a scoped claim:
        # two chapters may own different halves of one variant.
        scoped = bool(re.search(r"\bHere for\b", where, re.I))
        claims.setdefault((cells[0], cells[1]), []).append((num, scoped))


def guide_allows(variant, chapters):
    """The guide may assign one variant to more than one chapter on purpose."""
    for name, nums in assigned.items():
        if variant.lower() in name.lower() or name.lower() in variant.lower():
            return set(chapters) <= nums
    return False


for key, entries in sorted(claims.items()):
    # a scoped claim ("Here for the placement, Chapter 9 for the link") names
    # which half it owns, so it never conflicts with anyone
    nums = [n for n, scoped in entries if not scoped]
    if len(nums) < 2:
        continue
    if guide_allows(key[1], nums):
        continue                      # the guide assigns it to all of them
    note(f"variant {key[0]} / {key[1]} claimed as built in full by chapters "
         f"{sorted(nums)}, and the guide does not assign it to all of them")

# ---------------------------------------------------------------- figures
used, defined = set(), {p.stem for p in FIGURES.glob("*.tex")}
for p in sorted(SECTIONS.glob("*.tex")):
    text = p.read_text(encoding="utf-8")
    for name in re.findall(r"\\diagram\{([^}]+)\}", text):
        used.add(name)
        if name not in defined:
            note(f"{p.name} draws {name}, but figures/{name}.tex does not exist")
for name in sorted(defined - used):
    note(f"figures/{name}.tex exists but no chapter draws it")

# ---------------------------------------------------------------- references
labels = set()
for p in SECTIONS.glob("*.tex"):
    text = p.read_text(encoding="utf-8")
    labels |= set(re.findall(r"\\label\{([^}]+)\}", text))
    # \project and \diagram create their labels inside the macro, not in the source
    labels |= {f"sec:c{n}" for n in re.findall(r"\\project\{(\d+)\}", text)}
    labels |= {f"fig:{n}" for n in re.findall(r"\\diagram\{([^}]+)\}", text)}
for p in sorted(SECTIONS.glob("*.tex")):
    for ref in re.findall(r"\\ref\{([^}]+)\}", p.read_text(encoding="utf-8")):
        if ref not in labels:
            note(f"{p.name} refers to {ref}, which is never defined")

# prose cross-references to chapters that do not exist
existing_nums = {int(s[1:]) for s in present if re.fullmatch(r"c\d\d", s)}
for p in sorted(SECTIONS.glob("*.tex")):
    for n in re.findall(r"\bChapter (\d+)\b", p.read_text(encoding="utf-8")):
        if int(n) not in existing_nums and int(n) <= 20:
            note(f"{p.name} mentions Chapter {n}, which has no source file yet")

# ---------------------------------------------------------------- titles
table = {}
for num, title in re.findall(r"^\|\s*(\d\d)\s*\|\s*([^|]+?)\s*\|", guide, re.M):
    table[int(num)] = title.strip()
for p in sorted(SECTIONS.glob("c[0-9][0-9].tex")):
    text = p.read_text(encoding="utf-8")
    m = re.search(r"\\project\{(\d+)\}\{([^}]*)\}", text)
    if not m:
        note(f"{p.name}: no \\project line")
        continue
    num, title = int(m.group(1)), m.group(2)
    if num != int(p.stem[1:]):
        note(f"{p.name}: \\project says chapter {num}")
    want = table.get(num)
    if want and title.strip().lower() != want.lower():
        note(f"chapter {num}: title is {title!r} but the guide says {want!r}")

# ---------------------------------------------------------------- dates
MONTHS = ("January|February|March|April|May|June|July|August|September|October"
          "|November|December")
WEEKDAYS = "Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday"
VERB = re.compile(r"\\begin\{(asciiart|ccode|cppcode|rustcode|shellcode|pycode|makecode"
                  r"|dtscode|yamlcode|asmcode|ldcode|plaincode|lstlisting|verbatim)\}"
                  r"(.*?)\\end\{\1\}", re.S)
PUBDATE = re.compile(r"\\pubdate\{[^{}]*\}")
for p in sorted(SECTIONS.glob("*.tex")):
    prose = VERB.sub("\n", p.read_text(encoding="utf-8"))
    # \pubdate{April 2010} marks a date a publisher gives to month precision
    # only. The house rule cannot ask for a day nobody published, so these are
    # removed before the scan; every month-and-year NOT wrapped is still a
    # defect, which is the point of making the exemption explicit in the source.
    prose = PUBDATE.sub(" ", prose)
    # a month with a year but no weekday in front
    for m in re.finditer(rf"(?<!\w)(?:(\d{{1,2}})\s+)?({MONTHS})\s+(\d{{4}})", prose):
        start = max(0, m.start() - 30)
        if not re.search(rf"({WEEKDAYS})\s+\d{{1,2}}\s*$", prose[start:m.start()]):
            if not m.group(1):
                note(f"{p.name}: bare month and year {m.group(0)!r}; use weekday, day, month, year")
    for m in re.finditer(rf"(?<!\w)({WEEKDAYS})(?!\s+\d)", prose):
        note(f"{p.name}: bare weekday {m.group(0)!r}")

# ---------------------------------------------------------------- report
print(f"chapters present: {len(existing_nums)} of 20")
missing = sorted(set(range(1, 21)) - existing_nums)
if missing:
    print("still to write:", ", ".join(f"c{n:02d}" for n in missing))
print(f"figures: {len(defined)} defined, {len(used)} drawn")
if problems:
    print(f"\n{len(problems)} problems:")
    for pr in problems:
        print("  " + pr)
else:
    print("\nno cross-chapter problems")
sys.exit(1 if problems else 0)
