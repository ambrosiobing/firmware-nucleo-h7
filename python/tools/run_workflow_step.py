#!/usr/bin/env python3
"""Run a workflow step's own script, rather than a copy of it typed by hand.

WHY THIS EXISTS, AND IT IS ONE DEFECT. On Monday 26 October 2026 commit 9208639
added eight compiler invocations to `code.yml`'s warnings-as-errors step and
turned main red. Before pushing it, every one of those eight sources was
compiled on the laptop with a compiler, under the exact flag string, and all
eight were clean. The commit still failed.

The reason, found on Thursday 29 October 2026: **all eight added lines carried a
literal backslash-n where a YAML line continuation was intended.** Bash reads
`\\n` outside quotes as an escaped `n`, so each gcc received an argument `n`,
found no such file and exited 1. It would have failed under every compiler, and
the two sources already in the step pass under gcc 13, 14 and 15 alike, which is
what finally ruled the compiler out.

What was verified was the ARITHMETIC OF THE CHANGE and not the change. The
sources compiled; the step's text did not run. That is the error this
repository's own notes call running a copy instead of the real path, and the
remedy is this file: it reads the step out of the workflow and executes exactly
those bytes.

It also converts a claim this volume has repeated for three days. A workflow
step was said to be answerable only by the runner, because a branch push runs
nothing. That is true of what the runner provides, the image and the toolchain,
and NOT true of the script, which is just bash. The script can be run here.

USAGE, from the repository root:

    python3 python/tools/run_workflow_step.py --list
    python3 python/tools/run_workflow_step.py "The same sources with warnings as errors"
    python3 python/tools/run_workflow_step.py "..." --gcc 13

`--gcc N` puts a directory on PATH in which `gcc` and `g++` are the versioned
binaries, so the step's own unversioned `gcc` resolves to the one asked for.
Nothing is written outside a temporary directory and the build output the step
itself produces.

This tool runs nothing on win11 aquamarine: it refuses unless a compiler is on
PATH, because compiling there is forbidden by rule.
"""
from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"

NAME = re.compile(r"^(\s+)- name:\s*(.+?)\s*$")
RUN = re.compile(r"^(\s+)run:\s*\|\s*$")


def steps_in(path: Path):
    """Every step of a workflow as (name, script), the script de-indented.

    A plain text walk rather than a YAML parse, so this tool needs nothing
    installed that the suite does not already have. It reads only the block
    scalar form, `run: |`, which is the only form this repository uses for a
    multi-line step.
    """
    lines = path.read_text(encoding="utf-8").split("\n")
    out = []
    i = 0
    while i < len(lines):
        m = NAME.match(lines[i])
        if not m:
            i += 1
            continue
        name = m.group(2).strip().strip('"').strip("'")
        i += 1
        script = None
        while i < len(lines):
            if NAME.match(lines[i]):
                break
            r = RUN.match(lines[i])
            if r:
                indent = len(r.group(1)) + 2
                body = []
                i += 1
                while i < len(lines):
                    line = lines[i]
                    if line.strip() == "":
                        body.append("")
                        i += 1
                        continue
                    if len(line) - len(line.lstrip()) < indent:
                        break
                    body.append(line[indent:])
                    i += 1
                script = "\n".join(body).rstrip() + "\n"
                break
            i += 1
        if script is not None:
            out.append((name, script))
    return out


def all_steps():
    found = []
    for path in sorted(WORKFLOWS.glob("*.yml")):
        for name, script in steps_in(path):
            found.append((path, name, script))
    return found


def versioned_path(version: str) -> str:
    """A directory where gcc and g++ are the versioned binaries."""
    holder = tempfile.mkdtemp(prefix="stepcc-")
    for base in ("gcc", "g++"):
        real = shutil.which("{}-{}".format(base, version))
        if real is None:
            raise SystemExit(
                "{}-{} is not on PATH, so the step cannot be run under that "
                "compiler".format(base, version))
        link = Path(holder) / base
        try:
            os.symlink(real, link)
        except OSError:
            # No symlink permission: a one line shim does the same job.
            link.write_text('#!/bin/sh\nexec "{}" "$@"\n'.format(real),
                            encoding="utf-8")
            link.chmod(0o755)
    return holder


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("step", nargs="?", help="the step's name, exactly")
    ap.add_argument("--list", action="store_true",
                    help="print every step that has a run block")
    ap.add_argument("--gcc", metavar="N",
                    help="run with gcc and g++ resolving to version N")
    ap.add_argument("--print", action="store_true", dest="show",
                    help="print the script and run nothing")
    args = ap.parse_args()

    found = all_steps()
    if args.list:
        for path, name, script in found:
            print("{:<14} {} ({} lines)".format(path.name, name,
                                                script.count("\n")))
        return 0

    if not args.step:
        ap.error("name a step, or pass --list")

    matches = [(p, n, s) for p, n, s in found if n == args.step]
    if not matches:
        print("no step named {!r}. --list shows every one."
              .format(args.step), file=sys.stderr)
        return 2
    if len(matches) > 1:
        print("{} steps share that name, which this tool cannot resolve"
              .format(len(matches)), file=sys.stderr)
        return 2
    path, name, script = matches[0]

    if args.show:
        sys.stdout.write(script)
        return 0

    # The rule this repository works under: nothing compiles on the authoring
    # laptop. The check is for a compiler rather than for a platform, so a
    # future machine is judged by what it has.
    if shutil.which("gcc") is None and args.gcc is None:
        print("no gcc on PATH. This tool is for the laptop that compiles, "
              "which is WSL on the win11 skyhorizon demo laptop, "
              "bing@JPTOUPM678; win11 aquamarine compiles nothing by rule.",
              file=sys.stderr)
        return 3
    if platform.system() == "Windows":
        print("this tool runs the step under bash and is meant for WSL, not "
              "for a Windows shell", file=sys.stderr)
        return 3

    env = dict(os.environ)
    if args.gcc:
        env["PATH"] = versioned_path(args.gcc) + os.pathsep + env["PATH"]

    with tempfile.NamedTemporaryFile("w", suffix=".sh", delete=False,
                                     encoding="utf-8") as handle:
        handle.write(script)
        where = handle.name

    shown = subprocess.run(["bash", "-c", "gcc -dumpversion"], env=env,
                           capture_output=True, text=True)
    print("step:     {} ({})".format(name, path.name))
    print("compiler: gcc {}".format(shown.stdout.strip() or "unknown"))
    print("script:   {} lines, run verbatim".format(script.count("\n")))
    print()
    done = subprocess.run(["bash", where], cwd=str(ROOT), env=env)
    os.unlink(where)
    print()
    print("exit {}".format(done.returncode))
    return done.returncode


if __name__ == "__main__":
    raise SystemExit(main())
