"""Every path `build_host.py` names, checked on a laptop that cannot compile.

**Why this file exists, and it is a defect rather than a precaution.**
`build_host.py` is the only script in this repository that is never run on win11
aquamarine, because compiling there is forbidden. So a wrong path inside it is
invisible until WSL on the win11 skyhorizon demo laptop runs it, and on Sunday
4 October 2026 that is what happened: P06's two build steps had been written
against a project map that had no P06 key, and the result was a KeyError
traceback part way through a build, after four libraries had already been
produced.

The map is now derived from the tree, so that key cannot be missing again. This
file covers the other half, which deriving does not fix: a source file named in a
build step that is not there. It needs no compiler, so it runs everywhere.

**Why it reads the script's text rather than its variables.** The source lists
are built inside `main()`, so there is nothing to import that holds them. The
regular expression therefore has to find every call, and a regular expression
that quietly matched none would pass while checking nothing. So the count of
matches is asserted against the count of `proj("` occurrences in the file: a call
written in a shape the pattern does not recognise fails this test rather than
slipping past it.
"""
from __future__ import annotations

import importlib.util
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "python" / "tools" / "build_host.py"

CALL = re.compile(r'proj\("([A-Z]\d\d)",\s*"(\w+)"\)')
CALL_WITH_FILE = re.compile(r'proj\("([A-Z]\d\d)",\s*"(\w+)"\)\s*/\s*"([\w.]+)"')

# The shared trees under c/, which proj() knows nothing about. Added Sunday
# 4 October 2026 with the first build step that names one: c/clock/clocktree.c,
# the clock tree decode, which is shared because the firmware links it and is
# built here because four languages are compared against it. Until then every
# source in this script came from a project directory, and the hole was the same
# one this file exists to close: a path that silently does not exist.
SHARED_DIR = re.compile(r'C_DIR\s*/\s*"(\w+)"')
SHARED_FILE = re.compile(r'C_DIR\s*/\s*"(\w+)"\s*/\s*"([\w.]+)"')


def build_host():
    """The module itself, imported and not run. `main()` sits behind the usual
    guard, so importing compiles nothing and is safe on a laptop with no
    compiler."""
    spec = importlib.util.spec_from_file_location("build_host_under_test", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_the_pattern_finds_every_call_written_in_the_script():
    text = SCRIPT.read_text(encoding="utf-8")
    assert text.count('proj("') == len(CALL.findall(text)), (
        "a proj() call is written in a shape this test does not recognise, so it "
        "would not be checked. Widen the pattern rather than deleting this line."
    )


def test_every_project_and_language_directory_named_in_a_build_step_exists():
    module = build_host()
    text = SCRIPT.read_text(encoding="utf-8")
    pairs = sorted(set(CALL.findall(text)))
    assert pairs, "no build step names a project, which cannot be right"
    for key, lang in pairs:
        # proj() itself raises SystemExit with a readable message when either
        # half is wrong, so calling it is the check.
        path = module.proj(key, lang)
        assert path.is_dir()
    print("\n  {} project directories named by build_host.py, all present: {}".format(
        len(pairs), ", ".join("{}/{}".format(k, l) for k, l in pairs)))


def test_every_source_file_named_in_a_build_step_exists():
    module = build_host()
    text = SCRIPT.read_text(encoding="utf-8")
    named = sorted(set(CALL_WITH_FILE.findall(text)))
    assert named, "no build step names a source file, which cannot be right"
    missing = []
    for key, lang, filename in named:
        path = module.proj(key, lang) / filename
        if not path.is_file():
            missing.append(path.relative_to(ROOT).as_posix())
    assert not missing, "build_host.py names sources that are not there: {}".format(
        ", ".join(missing))
    print("  {} source files named by build_host.py, all present".format(len(named)))


def test_the_pattern_finds_every_shared_tree_the_script_names():
    text = SCRIPT.read_text(encoding="utf-8")
    assert text.count("C_DIR /") == len(SHARED_DIR.findall(text)), (
        "a C_DIR path is written in a shape this test does not recognise, so it "
        "would not be checked. Widen the pattern rather than deleting this line."
    )


def test_every_shared_directory_and_source_under_c_that_a_build_step_names_exists():
    module = build_host()
    text = SCRIPT.read_text(encoding="utf-8")
    dirs = sorted(set(SHARED_DIR.findall(text)))
    files = sorted(set(SHARED_FILE.findall(text)))
    assert dirs, "no build step names a shared tree under c/, which cannot be right"
    assert files, "no build step names a shared source under c/, which cannot be right"

    missing = [name for name in dirs if not (module.C_DIR / name).is_dir()]
    assert not missing, "build_host.py names shared trees that are not there: {}".format(
        ", ".join("c/" + name for name in missing))

    missing = [(d, f) for d, f in files if not (module.C_DIR / d / f).is_file()]
    assert not missing, "build_host.py names shared sources that are not there: {}".format(
        ", ".join("c/{}/{}".format(d, f) for d, f in missing))
    print("  {} shared trees and {} shared sources under c/, all present: {}".format(
        len(dirs), len(files), ", ".join("c/{}/{}".format(d, f) for d, f in files)))
