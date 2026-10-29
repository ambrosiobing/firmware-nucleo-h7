"""The host builder's own list of what it builds is compared with what it builds.

**The day this was needed, Thursday 29 October 2026.** `build_host.py` opens with
a docstring listing every artefact it produces, one line each, and that list had
drifted to twenty-two of thirty-seven: eight shared libraries and six C++ filters
registered between Monday 26 and Wednesday 28 October 2026 were simply absent
from it. Nobody read a wrong number; a reader looking for P07's or P20's C++
variant in that list would have concluded it did not exist.

**It is the same defect as the cross step in `code.yml`,** which claimed to build
"every `no_std` library" and built six of twelve for twenty-five days. Both were
hand-maintained lists next to the thing they described, and in both cases nothing
compared the two. That one was closed on Thursday 29 October 2026 by a test that
reads the step's crate list and compares it against every crate declaring
`no_std`; this closes the other half the same way.

**What it cannot do.** It compares names and not descriptions, so a line whose
prose is wrong about what a target is still passes. And it reads the two call
shapes this script actually uses, so a third shape would be invisible: the
lower bound below is what notices that, since a parser that stopped finding
targets would find far fewer than the thirty-seven known today.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD_HOST = ROOT / "python" / "tools" / "build_host.py"

# A shared library, named in the helper's first argument.
LIB = re.compile(r'shared_lib\(\s*"([A-Za-z0-9_]+)"')

# A binary, named in the label that is the last argument of a direct compiler
# invocation and sits alone on its own line, which is how every one of them is
# written in that script.
LABEL = re.compile(r'^\s*"([A-Za-z0-9_]+)"\)\s*$', re.M)

# The two targets whose names are computed per ordering mode rather than
# written out, and the docstring lines that cover them as ranges. A test that
# demanded a literal "ring0" line would be demanding the docstring say four
# things where saying one is clearer.
RANGES = {
    "ring{}": "ring0..ring3",
    "property_test{}": "property_test0..3",
}


def docstring_of(text: str) -> str:
    first = text.index('"""')
    second = text.index('"""', first + 3)
    return text[first + 3:second]


def test_every_host_target_appears_in_the_builders_own_list():
    text = BUILD_HOST.read_text(encoding="utf-8")
    doc = docstring_of(text)
    body = text[text.index('"""', text.index('"""') + 3) + 3:]

    targets = sorted(set(LIB.findall(body)) | set(LABEL.findall(body)))
    assert len(targets) >= 30, (
        "only {} targets were found in build_host.py, so this test has stopped "
        "reading the script it thinks it is reading: {}"
        .format(len(targets), ", ".join(targets)))

    missing = [t for t in targets
               if not re.search(r"\b{}\b".format(re.escape(t)), doc)]
    assert not missing, (
        "build_host.py builds these and its own opening list does not name "
        "them, so a reader looking for one would conclude it does not exist. "
        "Add a line for each to that docstring: {}".format(", ".join(missing)))

    # The computed names, covered as ranges rather than one line each.
    for spelling, expected in RANGES.items():
        assert spelling in body, (
            "{} is no longer built per ordering mode, so this test's exemption "
            "for it is stale".format(spelling))
        assert expected in doc, (
            "{} is built once per ordering mode and the list no longer carries "
            "the range {}".format(spelling, expected))

    print("\n  {} host targets, every one of them named in build_host.py's own "
          "opening list, plus {} built per ordering mode and listed as ranges"
          .format(len(targets), len(RANGES)))
