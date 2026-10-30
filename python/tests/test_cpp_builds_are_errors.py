"""Every C++ build in build_host.py treats its warnings as errors.

**The day this was needed, Friday 30 October 2026.** P19's C++ header named a
constant `kLine` beside an enum class member `Error::kLine`. gcc's `-Wshadow`
reported it; `build_host.py` printed the warning in the middle of forty build
lines and carried on; the suite then reported 1026 checks passing with every
parity row agreeing in four languages; and `check_c_syntax.py` reported 45
checked and 0 failed. **Nothing in the chain was red.** The only thing that
refused was a `g++ -Werror` command typed by hand beside the hand-over, which
had been added to five previous hand-overs and had never found anything until
that one.

**What the gap was.** `build_host.py`'s warning set is applied to every build it
makes, C and C++ alike, and carried no `-Werror`. The warnings-as-errors step in
`code.yml` names twelve C sources and no C++ at all. So a warning in any of the
twenty-one C++ sources printed and the build succeeded, and had done since the
first C++ filter was registered on Sunday 4 October 2026.

**Why this test rather than trust.** The remedy is one constant, `CXX_WARNINGS`,
and twenty call sites that use it. A twenty-first C++ build added later, by
copying one of the twenty, would carry it; added from scratch it might not, and
the failure mode is silence. That is the same shape as the cross step that
claimed every `no_std` library and built six of twelve, and as the builder's own
docstring that named twenty-two of thirty-seven targets, both found on Thursday
29 October 2026. A list beside the thing it describes needs something comparing
the two.

**What it cannot do.** It reads the flags written in this script and not what the
compiler receives, so a `-Wno-` added after `-Werror` in the same list would
satisfy it. And it says nothing about the C builds, which is a deliberate
asymmetry the script explains at the constant.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD_HOST = ROOT / "python" / "tools" / "build_host.py"

# Every C++ invocation in that script, from `run([cxx` to its closing bracket.
CXX_CALL = re.compile(r"run\(\[cxx.*?\)\n", re.S)


def test_every_cpp_build_in_the_builder_treats_warnings_as_errors():
    text = BUILD_HOST.read_text(encoding="utf-8")
    calls = CXX_CALL.findall(text)
    assert len(calls) >= 15, (
        "only {} C++ builds were found in build_host.py, so this test has "
        "stopped reading the script it thinks it is reading".format(len(calls)))

    without = [c.split("\n")[0].strip() for c in calls
               if "CXX_WARNINGS" not in c and "-Werror" not in c]
    assert not without, (
        "these C++ builds carry neither CXX_WARNINGS nor -Werror, so a warning "
        "in the source would print and the build would succeed: {}"
        .format("; ".join(without)))

    # And the constant itself has to carry the flag, or every call above is
    # satisfied by a name that promises nothing.
    assert re.search(r'CXX_WARNINGS\s*=\s*WARNINGS\s*\+\s*\[\s*"-Werror"',
                     text), (
        "CXX_WARNINGS no longer adds -Werror to the warning set, so the name "
        "every C++ build uses has stopped meaning what the builds rely on")

    print("\n  {} C++ builds in build_host.py, every one of them with "
          "warnings as errors".format(len(calls)))
