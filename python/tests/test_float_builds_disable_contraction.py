"""A host build whose source computes in floating point turns contraction off.

**What this is protecting, and the day it was needed.** On Monday 26 October 2026
P18's cascade was registered in `python/tools/build_host.py` without
`-ffp-contract=off` and the code workflow went red at the one step that runs
every test file. At -O2 gcc contracts a multiply followed by an add into a single
fused instruction, which rounds once where the two separate operations round
twice. The fused result is the more accurate of the two and the wrong one for this
repository, because every one of these libraries exists to be compared against a
Python twin that cannot fuse, and several of those comparisons are bit for bit on
purpose.

P06 and P12 already carried the flag, each with a comment explaining it, and the
third instance still reached main. That is what makes this a rule rather than a
habit worth remembering: nothing on the authoring laptop can compile, so the flag
cannot be exercised there, and a comment in two other projects is not a check.

**It read the source and not the source's includes, until Wednesday 28 October
2026.** P16's C++ filter is the first header-only library registered here: the
`.cpp` file names no floating point at all and every `double` in it lives in
`winfeat.hpp`, which this test never opened. So the flag it needs could not have
been required by the check written to require it, and the gap was found by
writing the file rather than by the check. It now follows a local include, one
level, from the directory of the named source, which is the only shape a
first-party include takes in this tree.

**What it still cannot do.** It reads a spelling rather than a semantics: a file
computing in floating point through a typedef this does not know about would
pass, and so would one whose floating point sits two includes deep. It also says
nothing about the Rust half, and nothing about the C++ halves beyond the filter
builds spelled in this one script.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD_HOST = ROOT / "python" / "tools" / "build_host.py"

FLAG = "-ffp-contract=off"

# The two shapes a host build takes in that script: the helper, and a direct
# compiler invocation for the C++ filters.
CALL_STARTS = ("shared_lib(", "run([cxx")

SOURCE = re.compile(r'"([A-Za-z0-9_./+-]+\.(?:c|cpp))"')
FLOAT_TYPE = re.compile(r"\b(?:float|double)\b")


def strip_comments(text: str) -> str:
    """Remove C comments, so a file that only mentions a type in prose is not
    reported. Crude on purpose: it does not know about string literals, and a
    source whose strings contain comment markers would be mangled here and
    nowhere else."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def calls(text: str):
    """Every build call, as the span from its opening bracket to its match."""
    for start in CALL_STARTS:
        at = 0
        while True:
            at = text.find(start, at)
            if at < 0:
                break
            open_at = text.index("(", at)
            depth = 0
            end = open_at
            for i in range(open_at, len(text)):
                if text[i] in "([{":
                    depth += 1
                elif text[i] in ")]}":
                    depth -= 1
                    if depth == 0:
                        end = i
                        break
            yield text[at:end + 1]
            at = end


def resolve(name: str):
    """A source named in the script, found by its spelling in the tree."""
    hits = [p for p in ROOT.rglob(name.split("/")[-1])
            if ".git" not in p.parts and "build-host" not in p.parts]
    return hits[0] if len(hits) == 1 else None


INCLUDE = re.compile(r'#\s*include\s*"([^"]+)"')


def computes_in_floating_point(path: Path):
    """The named source, or a header it includes from its own directory.

    Returns the path whose text names a floating point type, or None. One level
    of include and no further: that is the depth this tree actually uses, and a
    deeper walk would be a claim about a shape no file here has.
    """
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    if FLOAT_TYPE.search(text):
        return path
    for name in INCLUDE.findall(text):
        beside = path.parent / name
        if not beside.is_file():
            continue
        if FLOAT_TYPE.search(strip_comments(
                beside.read_text(encoding="utf-8", errors="replace"))):
            return beside
    return None


def test_every_floating_point_host_build_turns_contraction_off():
    text = BUILD_HOST.read_text(encoding="utf-8")
    checked = 0
    floating = []
    problems = []

    for span in calls(text):
        names = SOURCE.findall(span)
        if not names:
            continue
        for name in names:
            path = resolve(name)
            if path is None or not path.is_file():
                continue
            checked += 1
            # The source, or a header beside it, which is how P16's C++ filter
            # reaches a double without naming one.
            where = computes_in_floating_point(path)
            if where is None:
                continue
            floating.append(name if where == path
                            else "{} (through {})".format(name, where.name))
            if FLAG not in span:
                problems.append(name)

    assert checked >= 15, (
        "only {} sources were found in build_host.py's calls, so this test is "
        "no longer reading the script it thinks it is".format(checked))
    assert len(floating) >= 6, (
        "only {} of them compute in floating point, which is fewer than the six "
        "known on Monday 26 October 2026, so the detection has stopped working "
        "rather than the sources having changed".format(len(floating)))
    assert not problems, (
        "these host builds compile a source that computes in floating point "
        "without {}, so their results can differ from a Python twin in the last "
        "bits depending on what the compiler chose to fuse: {}"
        .format(FLAG, ", ".join(sorted(set(problems)))))
    print("\n  {} sources in host builds, {} of them floating point, all with {}"
          .format(checked, len(floating), FLAG))
