#!/usr/bin/env python3
"""Build everything the host test suite needs, without CMake.

    python python/tools/build_host.py

Why this exists. The book's build lines are CMake ones, and they are right on a
machine with a generator installed. On win11 aquamarine, re-checked
Thursday 1 October 2026, there is CMake 4.4.2 and Python 3.12.10 but no make and
no ninja, so CMake has nothing to drive, and no arm-none-eabi-gcc either. This
script calls the compiler directly so the whole host half of this repository can
be finished and proven today.

It is not a second build system. It takes no options and it will be deleted the
day ninja is installed.

What it builds. This list had fallen four libraries behind by Monday
5 October 2026, which is its own small lesson about lists maintained by hand, so
it is now in the order the script produces them:

  freqmath.{dll,so}       the frequency counter's arithmetic, split out of
                          freqcount.c so a host can test the divide
  pwmmath.{dll,so}        the signal source's arithmetic, split out of the TIM1
                          driver for the same reason, before that driver existed
  adcmath.{dll,so}        the converter's arithmetic and its four part-identity
                          decisions, split out of P06's back ends
  clocktree.{dll,so}      P01's clock tree decode, the same object the board links
  clocktree_filter        and its C++ variant, as a filter the tests drive
  payload.{dll,so}        P09's codec, from the same source the board links
  cpp_filter              P09's C++ variant
  frame.{dll,so}          P05's COBS, CRC-16 and frame, host and target alike
  frame_filter            P05's C++ variant
  node_sm.{dll,so}        P08's state machine, linking P09's codec
  node_sm_filter          P08's C++ variant
  rxspan.{dll,so}         P04's span arithmetic
  zonetrack.{dll,so}      P15's hysteresis
  rxspan_filter           P04's C++ variant
  atparse.{dll,so}        P11's classifier, deadline test and backoff
  atparse_filter          P11's C++ variant
  phaseledger.{dll,so}    P10's per-phase charge ledger
  phaseledger_filter      P10's C++ variant
  dspaccept.{dll,so}      P18's cascade in three formats and its two metrics
  dspaccept_filter        P18's C++ variant
  fifodecode.{dll,so}     P13's buffer decode, drop count and watermark
  recdecode.{dll,so}      P14's record decoder, altitude and drain time
  cyclecharge.{dll,so}    P07's cycle segmentation, spread and battery sum
  cyclecharge_filter      P07's C++ variant
  winfeat.{dll,so}        P16's window features and bounded tree
  winfeat_filter          P16's C++ variant
  tbmath.{dll,so}         P17's elapsed ticks, wrap periods and report
  tbmath_filter           P17's C++ variant
  mpualign.{dll,so}       P19's maintenance range and region encoding
  latstat.{dll,so}        P20's latency histogram, percentiles and verdict
  latstat_filter          P20's C++ variant
  rate.{dll,so}           P06's rate witness
  rate_filter             P06's C++ variant
  gates.{dll,so}          P12's size and charge gates
  gates_filter            P12's C++ variant
  attribute.{dll,so}      P03's attribution
  attribute_filter        P03's C++ variant
  ring_filter             P02's C++ variant
  ring0..ring3.{dll,so}   P02's ring, once per ordering mode
  property_test0..3       P02's property test, once per ordering mode
  soak_threads            P02's two-thread soak
  object sizes            for the tables in the project READMEs

The Rust filters are not here: cargo builds those, and conftest.py looks for
them under target/release.

THAT LIST IS A CHECKED CLAIM SINCE THURSDAY 29 OCTOBER 2026 and was wrong until
that day: it named twenty-two of the thirty-seven targets below, missing eight
shared libraries and six C++ filters added between Monday 26 and Wednesday 28
October 2026. Nothing compared the two, which is the same defect the cross step
in code.yml had for twenty-five days, so the remedy is the same one:
python/tests/test_build_host_lists_what_it_builds.py reads the calls in this
file and requires each target to appear in this docstring.
"""
from __future__ import annotations

import os
import subprocess
import sys
import time
from pathlib import Path
from shutil import which

ROOT = Path(__file__).resolve().parent.parent.parent   # python/tools -> repo root
BUILD = ROOT / "build-host"
C_DIR    = ROOT / "c"          # the shared C: board, ring, payload, instr, ld
PROJECTS = ROOT / "projects"

# Each project owns its own c/, cpp/, python/ and rust/ subdirectory, so a source
# file is found from the project rather than from a language tree.
#
# Derived from the tree and not written out by hand, and the previous version of
# this comment is the reason. It listed five projects and argued that a missing
# key would be "a clearer failure than a path that silently does not exist". It
# was neither clear nor caught. P06's build steps were added on Saturday
# 3 October 2026 against a map that had no P06 key, and the result was a KeyError
# traceback part way through a build in WSL on Sunday 4 October 2026, after four
# libraries had already been produced. Nothing on win11 aquamarine could have
# found it either, because compiling there is forbidden and this file is
# therefore never run there.
#
# Keyed by the PNN prefix of each project directory, so the twenty keys exist
# whether or not this script builds anything for them, and a wrong key is a
# message naming the twenty rather than a traceback.
PROJECT_DIR = {d.name.split("-", 1)[0]: d
               for d in sorted(PROJECTS.iterdir()) if d.is_dir()}


def proj(key, lang):
    """The directory holding one project's sources in one language."""
    if key not in PROJECT_DIR:
        raise SystemExit(
            "build_host.py asks for project {!r}, which is not a directory "
            "under projects/. The twenty that are: {}".format(
                key, ", ".join(sorted(PROJECT_DIR))))
    path = PROJECT_DIR[key] / lang
    if not path.is_dir():
        raise SystemExit(
            "build_host.py asks for {} of project {}, and {} is not a "
            "directory".format(lang, key, path.relative_to(ROOT).as_posix()))
    return path

# The Qt install is the only compiler on this laptop. Named explicitly rather
# than only looked for on PATH, because it is not on PATH and a silent fallback
# to some other compiler would make the size figures meaningless.
QT_BIN = Path(r"C:\Qt\Tools\mingw1310_64\bin")

WARNINGS = [
    "-Wall", "-Wextra", "-Wshadow", "-Wconversion", "-Wdouble-promotion",
    "-Wsign-conversion", "-Wpedantic",
]

# Windows only, and the reason is specific. A produced executable that links
# pthread needs libwinpthread-1.dll from the compiler's bin directory at run
# time, and without it Windows refuses to start the process with
# STATUS_DLL_NOT_FOUND, which surfaces as exit code 3221225781. Linking static
# removes that dependency entirely.
#
# On Linux this must stay empty. There, -static links libc statically, and a
# statically linked pthread_create needs -Wl,--whole-archive to behave, so
# adding it would trade a Windows problem for a Linux one. Nothing on Linux
# needs it: the shared libraries are found by the loader in the normal way.
STATIC = ["-static"] if sys.platform == "win32" else []


def find(name: str) -> str:
    found = which(name)
    if found:
        return found
    candidate = QT_BIN / (name + ".exe")
    if candidate.exists():
        return str(candidate)
    raise SystemExit(
        "cannot find {}. On win11 aquamarine the only compiler is the Qt one at\n"
        "  {}\n"
        "If that path has changed, edit QT_BIN in this file.".format(name, QT_BIN)
    )


def shared_library_name(stem: str) -> str:
    return "{}.dll".format(stem) if sys.platform == "win32" else "lib{}.so".format(stem)


def compiler_env(compiler: str) -> dict:
    """The environment every compiler invocation needs, with its bin on PATH.

    This is not optional on Windows and it is the single hardest thing this
    script has got wrong. gcc.exe loads happily because its DLLs sit in the same
    directory, which is why `gcc --version` succeeds. The real compiler is
    cc1.exe under libexec, and that needs libwinpthread-1.dll and
    libgcc_s_seh-1.dll from the compiler's bin directory. Without that directory
    on PATH, cc1.exe dies with STATUS_DLL_NOT_FOUND before writing anything, and
    gcc reports exit status 1 with no diagnostic at all.

    Diagnosed Thursday 1 October 2026, after two wrong guesses about file
    locking. It had looked like a machine-specific fault because the shell that
    worked happened to carry C:\\Program Files\\Git\\mingw64\\bin on PATH, which
    holds compatible copies of those libraries. Finding this by accident is
    exactly why the build must set PATH itself rather than depend on the shell.
    """
    env = dict(os.environ)
    bin_dir = str(Path(compiler).parent)
    env["PATH"] = bin_dir + os.pathsep + env.get("PATH", "")
    return env


def clear_target(path: Path) -> None:
    """Get an existing output file out of the way before the linker writes it.

    Windows will not let a loaded or scanned file be overwritten, and the linker's
    failure for that is exit status 1 with, on Thursday 1 October 2026, no message
    at all. Checking the file is closable first is not enough: it was closable
    when checked and locked a moment later when ld wrote it, which is what a
    virus scanner touching a freshly created DLL looks like.

    The fix is the standard Windows one. A file that cannot be deleted can almost
    always still be renamed, because the rename changes the directory entry and
    not the open handle. So move it aside and let the compiler create a fresh
    one. The stale copy is deleted on the next run once nothing holds it.
    """
    if not path.exists():
        return
    try:
        path.unlink()
        return
    except OSError:
        pass
    aside = path.with_name("{}.stale-{}".format(path.name, os.getpid()))
    try:
        os.replace(path, aside)
    except OSError as exc:
        raise SystemExit(
            "{} cannot be removed or renamed: {}\n"
            "Something holds it and will not let go. Close any other window "
            "running pytest,\nand any Python session that imported it through "
            "ctypes, then run this again.".format(path.name, exc)
        ) from None
    try:
        aside.unlink()
    except OSError:
        pass            # still held; it is ignored by git and goes next time


def sweep_stale() -> None:
    """Delete the aside copies from earlier runs, now that nothing holds them."""
    for stale in BUILD.glob("*.stale-*"):
        try:
            stale.unlink()
        except OSError:
            pass


def run(cmd: list[str], label: str) -> None:
    """Run one compiler invocation, and on failure show what it actually said.

    The first version of this used subprocess.run(check=True), which raises a
    CalledProcessError carrying the argument list and not the diagnostic. On
    Thursday 1 October 2026 that cost a round trip: a build failed with gcc exit
    status 1 and the traceback showed the whole command line and none of the
    reason. A build script that hides the compiler's own message is worse than no
    build script.
    """
    print("  " + label)

    # Everything this script builds is written with -o as the last argument.
    # Clear that file first, so the linker always creates rather than replaces.
    if "-o" in cmd:
        clear_target(Path(str(cmd[cmd.index("-o") + 1])))

    proc = subprocess.run(
        [str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True,
        env=compiler_env(str(cmd[0])),
    )

    # One retry, because the failure this guards against is a race with whatever
    # scanned or loaded the file, and a race that lost once usually wins next.
    if proc.returncode != 0 and "-o" in cmd:
        target = Path(str(cmd[cmd.index("-o") + 1]))
        if not proc.stdout.strip() and not proc.stderr.strip():
            print("    no output from the compiler, retrying once")
            time.sleep(0.5)
            clear_target(target)
            proc = subprocess.run(
                [str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True,
                env=compiler_env(str(cmd[0])),
            )

    if proc.returncode == 0:
        if proc.stderr.strip():                 # warnings, worth seeing
            print(indent(proc.stderr.strip()))
        return

    print("\nFAILED: {}".format(label))
    print("  exit status {}".format(proc.returncode))
    if proc.stdout.strip():
        print(indent(proc.stdout.strip()))
    if proc.stderr.strip():
        print(indent(proc.stderr.strip()))
    print("\n  the command was:")
    print(indent(" ".join(str(c) for c in cmd), "    "))

    diagnose(proc.stderr + proc.stdout)
    raise SystemExit(1)


def indent(text: str, prefix: str = "  | ") -> str:
    return "\n".join(prefix + line for line in text.splitlines())


def diagnose(output: str) -> None:
    """Name the failures that are about this machine rather than about the code."""
    if not output.strip():
        print(
            "\n  The compiler said nothing at all and still failed. On Windows\n"
            "  that almost always means one of its own helper executables could\n"
            "  not load its libraries. gcc.exe itself runs, because its DLLs sit\n"
            "  beside it, which is why `gcc --version` works. The real compiler\n"
            "  is cc1.exe under libexec, and it needs libwinpthread-1.dll and\n"
            "  libgcc_s_seh-1.dll from the compiler's bin directory. Without that\n"
            "  directory on PATH, cc1.exe dies with STATUS_DLL_NOT_FOUND before\n"
            "  writing anything and gcc returns 1 with no message.\n"
            "\n"
            "  compiler_env() in this file puts that directory on PATH for every\n"
            "  invocation, so seeing this now means the compiler install is\n"
            "  incomplete rather than merely unreachable. Check that\n"
            "  libwinpthread-1.dll and libgcc_s_seh-1.dll are in the same\n"
            "  directory as gcc.exe, and run --doctor."
        )
    elif "cannot open output file" in output and "No such file or directory" in output:
        print(
            "\n  The build directory is missing. That should not happen, since\n"
            "  this script creates it, so check whether something deleted\n"
            "  build/ while the script was running."
        )
    elif "cannot open output file" in output and (
        "Permission denied" in output or "Invalid argument" in output
    ):
        print(
            "\n  Windows will not let a loaded library be overwritten. Something\n"
            "  still has it open, which is almost always a Python process that\n"
            "  imported it through ctypes: a pytest run in another window, or a\n"
            "  session left at a prompt after `import ctypes; ctypes.CDLL(...)`.\n"
            "  Close it and run this again. Nothing is wrong with the source."
        )
    elif "cannot find -lpthread" in output or "pthread" in output.lower():
        print(
            "\n  The pthread library is missing. The Qt mingw toolchain is the\n"
            "  posix-threads build and carries it; a win32-threads build does\n"
            "  not. Check that QT_BIN in this file still points at\n"
            "  mingw1310_64, whose triplet is x86_64-posix-seh."
        )


def doctor() -> int:
    """Report facts about this machine instead of guessing about it.

        python python/tools/build_host.py --doctor

    Written on Thursday 1 October 2026 after two wrong diagnoses of a build that
    failed with gcc exit status 1 and no output at all, on one machine, in one
    shell session, and nowhere else. Guessing twice is enough.
    """
    print("build_host doctor\n")

    print("paths")
    print("  ROOT   {}".format(ROOT))
    print("  BUILD  {}  exists={}".format(BUILD, BUILD.is_dir()))
    print("  python {}".format(sys.executable))
    print("  cwd    {}".format(Path.cwd()))

    print("\nenvironment that the compiler depends on")
    for var in ("TMP", "TEMP", "TMPDIR", "VIRTUAL_ENV", "CPATH", "C_INCLUDE_PATH",
                "LIBRARY_PATH", "LD_LIBRARY_PATH", "GCC_EXEC_PREFIX",
                "COMPILER_PATH", "CFLAGS", "LDFLAGS"):
        value = os.environ.get(var)
        print("  {:<18} {}".format(var, value if value is not None else "(unset)"))
        if var in ("TMP", "TEMP", "TMPDIR") and value:
            print("  {:<18} writable={}".format("", _writable(Path(value))))

    try:
        cc, cxx = find("gcc"), find("g++")
    except SystemExit as exc:
        print("\n{}".format(exc))
        return 1
    print("\ncompilers")
    print("  gcc  {}".format(cc))
    print("  g++  {}".format(cxx))
    for name, path in (("gcc", cc), ("g++", cxx)):
        proc = subprocess.run([path, "--version"], capture_output=True, text=True,
                              env=compiler_env(path))
        first = (proc.stdout or proc.stderr).splitlines()
        print("  {} --version -> exit {} : {}".format(
            name, proc.returncode, first[0] if first else "(no output)"))

    print("\ncan this process write into build/ at all")
    BUILD.mkdir(parents=True, exist_ok=True)
    probe = BUILD / "doctor-probe.bin"
    try:
        probe.write_bytes(b"probe")
        probe.unlink()
        print("  yes")
    except OSError as exc:
        print("  NO: {}".format(exc))

    print("\nminimal compile, no linking")
    src = BUILD / "doctor.c"
    src.write_text("int doctor(void) { return 0; }\n", encoding="utf-8")
    obj = BUILD / "doctor.o"
    _probe_step([cc, "-std=c11", "-c", str(src), "-o", str(obj)], "compile to object")

    print("\nminimal link to a shared library")
    dll = BUILD / ("doctor" + (".dll" if sys.platform == "win32" else ".so"))
    _probe_step([cc, "-shared", str(obj), "-o", str(dll)], "link object to library")

    print("\nthe real thing, in one step, exactly as the build does it")
    target = BUILD / ("doctor_payload" + (".dll" if sys.platform == "win32" else ".so"))
    _probe_step(
        [cc, "-std=c11", "-O2", *WARNINGS, "-shared", "-fPIC",
         "-I", str(C_DIR / "payload"), str(C_DIR / "payload" / "payload.c"), "-o", str(target)],
        "payload.c to a shared library")

    for leftover in (src, obj, dll, target):
        try:
            leftover.unlink()
        except OSError:
            pass

    print("\nIf the one-step build is the only failure, the compiler needs a\n"
          "writable temporary directory for its intermediate file and TMP or\n"
          "TEMP above is the thing to look at. If every step fails, the problem\n"
          "is the compiler install or something intercepting it. Either way this\n"
          "output says which, and no further guessing is needed.")
    return 0


def _writable(directory: Path) -> bool:
    try:
        probe = directory / "doctor-probe.tmp"
        probe.write_bytes(b"x")
        probe.unlink()
        return True
    except OSError:
        return False


def _probe_step(cmd: list[str], label: str) -> None:
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT,
                          env=compiler_env(cmd[0]))
    status = "ok" if proc.returncode == 0 else "FAILED exit {}".format(proc.returncode)
    print("  {:<34} {}".format(label, status))
    out = (proc.stdout + proc.stderr).strip()
    if out:
        print(indent(out, "      | "))
    elif proc.returncode != 0:
        print("      | the compiler produced no output at all")


def main() -> int:
    if "--doctor" in sys.argv[1:]:
        return doctor()

    BUILD.mkdir(parents=True, exist_ok=True)
    if not BUILD.is_dir():
        raise SystemExit("{} exists and is not a directory".format(BUILD))
    cc, cxx = find("gcc"), find("g++")
    exe = ".exe" if os.name == "nt" else ""

    # Aside copies left by an earlier run, now that nothing holds them. An
    # earlier version checked each library was closable before building, which
    # was not enough: the file was closable when checked and locked a moment
    # later when the linker wrote it. clear_target() in run() handles that by
    # renaming rather than by asking first.
    sweep_stale()

    def shared_lib(stem: str, sources: list, defines: list = [],
                   includes: list = []) -> None:
        """Compile then link, as two steps rather than one.

        One combined -shared invocation makes the compiler use a temporary file
        for the intermediate object, in whatever directory TMP points at, and
        hides which half failed. Two steps put every intermediate inside build/,
        where this script has already proved it can write, and a failure names
        its stage. Added Thursday 1 October 2026 after a one-step build failed
        with exit status 1 and no diagnostic on one machine only.
        """
        objects = []
        for source in sources:
            source = Path(str(source))
            obj = BUILD / "{}-{}.o".format(stem, source.stem)
            inc = []
            for d in includes:
                inc += ["-I", d]
            run([cc, "-std=c11", "-O2", *WARNINGS, *defines, "-fPIC",
                 *inc, "-c", source, "-o", obj],
                "{}: compile {}".format(stem, source.name))
            objects.append(obj)
        run([cc, "-shared", "-static-libgcc", *objects,
             "-o", BUILD / shared_library_name(stem)],
            "{}: link".format(stem))

    # P01's clock tree decode. The only shared library here that is built from
    # c/ rather than from a project directory, and the reason is that the
    # firmware links the same file: c/clock/clocktree.c is what system.c calls,
    # so this is a test of the shipped decode rather than of a host copy of it.
    # It compiles on a host at all only because it takes the seven register
    # VALUES as an argument and dereferences nothing.
    # The frequency counter's arithmetic, split out of c/instr/freqcount.c on
    # Monday 5 October 2026 for the same reason the clock tree was split out of
    # c/board/system.c: the register work cannot be called on a host, so the
    # divide had no test. freqmath.c reads nothing and is the same object the
    # firmware links.
    print("The frequency counter's arithmetic, the same file the board links:")
    shared_lib("freqmath", [C_DIR / "instr" / "freqmath.c"],
               includes=[C_DIR / "instr"])

    # The signal source's arithmetic. Split out BEFORE its driver was written
    # rather than after, which is the only one of the three splits in this
    # repository that was not a correction: clocktree.c came out of system.c and
    # freqmath.c out of freqcount.c once the untestability was noticed, and this
    # one starts on the right side of the line.
    print("The signal source's arithmetic, split out before its driver exists:")
    shared_lib("pwmmath", [C_DIR / "instr" / "pwmmath.c"],
               includes=[C_DIR / "instr"])

    # The converter's arithmetic, split out on Tuesday 6 October 2026 for a
    # reason the other three did not have: the bring-up turned out to depend on
    # part identity at FOUR points, every one behind a preprocessor condition in
    # ST's HAL, so the decisions needed somewhere a table of cases could reach
    # them before any register was written. P06's README carries all four.
    print("The converter's arithmetic, split out before its driver exists:")
    shared_lib("adcmath", [C_DIR / "instr" / "adcmath.c"],
               includes=[C_DIR / "instr"])

    print("P01, the clock tree decode, the same file the board links:")
    shared_lib("clocktree", [C_DIR / "clock" / "clocktree.c"],
               includes=[C_DIR / "clock"])

    print("P01, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         *STATIC,
         "-I", proj("P01", "cpp"),
         proj("P01", "cpp") / "clocktree_filter.cpp",
         "-o", BUILD / ("clocktree_filter" + exe)],
        "clocktree_filter")

    print("P09, the codec, from the same payload.c the board links:")
    shared_lib("payload", [C_DIR / "payload" / "payload.c"], includes=[C_DIR / "payload"])

    print("P09, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         *STATIC,
         "-I", C_DIR / "payload", "-I", proj("P09", "cpp"),
         proj("P09", "cpp") / "cpp_filter.cpp",
         "-o", BUILD / ("cpp_filter" + exe)],
        "cpp_filter")

    print("P05, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         *STATIC,
         "-I", proj("P05", "cpp"),
         proj("P05", "cpp") / "frame_filter.cpp",
         "-o", BUILD / ("frame_filter" + exe)],
        "frame_filter")

    print("P05, the framing layer: COBS, CRC-16 and the frame, as one library:")
    shared_lib("frame",
               [proj("P05", "c") / "frame.c", proj("P05", "c") / "cobs.c",
                proj("P05", "c") / "crc16.c"],
               includes=[proj("P05", "c")])

    # P04's first code, added Monday 26 October 2026. The project had nothing at
    # all until then and now has the one part of chapter 4 that needs no
    # hardware: the arithmetic that turns two transfer-counter positions into at
    # most two spans. The counter read, the three handlers and the cache
    # invalidate still need the board, and the project's status line says host
    # rather than board for that reason.
    # P15's first code, Monday 26 October 2026, and scoped to what the chapter
    # actually specifies. Figure 15.4 draws the four states with their
    # conditions and step 9 gives the thresholds; the validity filter and the
    # status byte need six constants the chapter gives nowhere, and this project
    # does not invent them.
    # P11's first code, Monday 26 October 2026: the three pieces of chapter 11
    # that need no module. The engine, its queue and the hundred injected faults
    # need the modem and the byte path of chapter 4.
    # P10's first code, Monday 26 October 2026: the arithmetic that turns a
    # current trace and a marker code per sample into charge per phase. The
    # marker pins, the single store and the power profiler are board work.
    # -ffp-contract=off, added Monday 26 October 2026 after P18 was registered
    # without it and turned the code workflow red. This library escaped the same
    # failure only because every case in its oracle is exact in binary, so no
    # rounding happens and there is nothing for a fused multiply-add to round
    # differently. That is one inexact case away from the same red, and the
    # charge ledger's two routes to a total are exactly the pattern gcc fuses.
    print("P10, the per-phase charge ledger and its residual:")
    shared_lib("phaseledger", [proj("P10", "c") / "phaseledger.c"],
               ["-ffp-contract=off"], includes=[proj("P10", "c")])

    # P10's C++ variant, Thursday 29 October 2026, and it completes the fourth
    # language for this project. std::variant turns the C header's prose promise
    # ("Returns 0, or one of the refusals above having written nothing") into
    # the type of the answer, which is P04's answer a second time.
    # -ffp-contract=off because the comparison is exact and the two routes to a
    # total are the shape gcc fuses at -O2; the guard requires it by reading the
    # header as well as this source.
    print("P10, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         "-ffp-contract=off", *STATIC,
         "-I", proj("P10", "cpp"),
         proj("P10", "cpp") / "phaseledger_filter.cpp",
         "-o", BUILD / ("phaseledger_filter" + exe)],
        "phaseledger_filter")

    print("P11, the line classifier, the wrap-safe deadline and the backoff:")
    shared_lib("atparse", [proj("P11", "c") / "atparse.c"],
               includes=[proj("P11", "c")])

    # P11's C++ variant, Thursday 29 October 2026, which completes that project
    # to four languages. Two C conventions become types: std::optional for the
    # 0xFFFFFFFF sentinel, which is a legal delay, and a sized table of
    # string_view for the null terminated array of prefixes. Integer arithmetic
    # throughout, so no contraction flag applies.
    print("P11, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         *STATIC,
         "-I", proj("P11", "cpp"),
         proj("P11", "cpp") / "atparse_filter.cpp",
         "-o", BUILD / ("atparse_filter" + exe)],
        "atparse_filter")

    print("P15, the hysteresis that stops the status byte chattering:")
    shared_lib("zonetrack", [proj("P15", "c") / "zonetrack.c"],
               includes=[proj("P15", "c")])

    print("P04, the span arithmetic of the idle-line receive path:")
    shared_lib("rxspan", [proj("P04", "c") / "rxspan.c"],
               includes=[proj("P04", "c")])

    # P18's first code, Monday 26 October 2026: one cascade in three numeric
    # formats and the two metrics chapter 18 judges them by. The transform, the
    # cycle counter and the placement grid need the part; deciding whether a
    # format can reach the tolerance does not, and that is the chapter's own
    # stated order of work.
    # -ffp-contract=off, for the third time in this repository and the first
    # time it was needed before anybody asked for it. P06 and P12 both carry it
    # with a comment saying why: at -O2 gcc contracts a multiply and an add into
    # one fused instruction, the fused result is correctly rounded once where the
    # separate operations round twice, and a Python twin does not fuse. This
    # project is denser in floating point than either of those and was registered
    # without the flag, so its exact cases passed, where no rounding happens at
    # all, and the 1024-sample design comparison did not.
    # P04's C++ variant, Thursday 29 October 2026. std::variant is the same sum
    # type the Rust gets from an enum, and std::visit over it is checked for
    # exhaustiveness at compile time; the crate README records what the
    # ceremony costs. Integer arithmetic throughout, so no contraction flag.
    print("P04, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         *STATIC,
         "-I", proj("P04", "cpp"),
         proj("P04", "cpp") / "rxspan_filter.cpp",
         "-o", BUILD / ("rxspan_filter" + exe)],
        "rxspan_filter")

    print("P18, the cascade in three formats and the acceptance metrics:")
    shared_lib("dspaccept", [proj("P18", "c") / "dspaccept.c"],
               ["-ffp-contract=off"], includes=[proj("P18", "c")])

    # P18's C++ variant, Monday 26 October 2026. One function template at
    # double and at float where the C carries two forty-line copies, which is
    # the one place in this project where the language says something about the
    # problem. -ffp-contract=off for the same reason the C has it.
    print("P18, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         "-ffp-contract=off", *STATIC,
         "-I", proj("P18", "cpp"),
         proj("P18", "cpp") / "dspaccept_filter.cpp",
         "-o", BUILD / ("dspaccept_filter" + exe)],
        "dspaccept_filter")

    # P13's first code, Monday 26 October 2026: the decode of a drained buffer,
    # the drop count recovered from the tag counter and the fill-to-drain ratio.
    # -ffp-contract=off because the ratio is floating point and is compared
    # against a twin, which is now a rule rather than a habit:
    # python/tests/test_float_builds_disable_contraction.py enforces it.
    print("P13, the buffer decode, the drop count and the watermark sizing:")
    shared_lib("fifodecode", [proj("P13", "c") / "fifodecode.c"],
               ["-ffp-contract=off"], includes=[proj("P13", "c")])

    # P14's first code, Monday 26 October 2026: chapter 14's record, read
    # through the scale codes it carries rather than through constants, plus the
    # barometric relation and the drain time step 10 says to compute first.
    # -ffp-contract=off for the reason the guard now enforces.
    print("P14, the record decoder, the altitude and the drain time:")
    shared_lib("recdecode", [proj("P14", "c") / "recdecode.c"],
               ["-ffp-contract=off"], includes=[proj("P14", "c")])

    # P07's first code, Monday 26 October 2026, and chapter 7 step 9 asked for
    # it in these words: the segmentation is the fragile part, so it is tested
    # on the host against a trace whose answer is known by construction, in
    # continuous integration, with no instrument attached.
    print("P07, the cycle segmentation, the spread and the battery sum:")
    shared_lib("cyclecharge", [proj("P07", "c") / "cyclecharge.c"],
               ["-ffp-contract=off"], includes=[proj("P07", "c")])

    # P16's first code, Tuesday 27 October 2026, and it is chapter 16's
    # criterion 1: the equivalence gate step 4 says to build before any leg.
    # Integer arithmetic throughout, so no contraction flag applies.
    # The stem is winfeat and not features: glibc ships its own <features.h>,
    # reached from <stdint.h>, and a -I directory holding that name shadows it,
    # which fails inside glibc rather than in this file.
    # P07's C++ variant, Thursday 29 October 2026. -ffp-contract=off because
    # this project compares double with == and no tolerance, and gcc at -O2
    # fuses a multiply and an add unless told not to. The guard in
    # test_float_builds_disable_contraction.py reads the header as well as the
    # source since Wednesday 28 October 2026, so it requires this flag rather
    # than merely not noticing its absence.
    print("P07, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         "-ffp-contract=off", *STATIC,
         "-I", proj("P07", "cpp"),
         proj("P07", "cpp") / "cyclecharge_filter.cpp",
         "-o", BUILD / ("cyclecharge_filter" + exe)],
        "cyclecharge_filter")

    print("P16, the window features and the bounded tree, which are the gate:")
    shared_lib("winfeat", [proj("P16", "c") / "winfeat.c"],
               includes=[proj("P16", "c")])

    # P16's C++ variant, Wednesday 28 October 2026, and it is the fourth
    # independent route to the floored integer square root: a corrected sqrt,
    # where the C descends bit by bit, the Python calls math.isqrt and the Rust
    # calls u64::isqrt. Criterion 1 is worth running in proportion to how
    # independently its implementations were written, and it has already paid
    # once.
    # -ffp-contract=off because that sqrt computes in floating point. NOTE THAT
    # THE GUARD IN test_float_builds_disable_contraction.py COULD NOT HAVE
    # REQUIRED THIS FLAG when the file was written: it reads the source named
    # here for the words float and double, and here they are in winfeat.hpp,
    # which it did not open. The guard now follows a local include, and that
    # gap is written up in the test.
    print("P16, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         "-ffp-contract=off", *STATIC,
         "-I", proj("P16", "cpp"),
         proj("P16", "cpp") / "winfeat_filter.cpp",
         "-o", BUILD / ("winfeat_filter" + exe)],
        "winfeat_filter")

    # P17's first code, Tuesday 27 October 2026: the timebase arithmetic, which
    # is the part of chapter 17 that needs no peripheral. Named tbmath to match
    # c/instr/freqmath.c and its siblings, the arithmetic split out of a file
    # that touches registers so it can be tested on a host.
    # -ffp-contract=off because the wrap period divides by a frequency.
    print("P17, the elapsed ticks, the wrap periods and the report:")
    shared_lib("tbmath", [proj("P17", "c") / "tbmath.c"],
               ["-ffp-contract=off"], includes=[proj("P17", "c")])

    print("P17, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         "-ffp-contract=off", *STATIC,
         "-I", proj("P17", "cpp"),
         proj("P17", "cpp") / "tbmath_filter.cpp",
         "-o", BUILD / ("tbmath_filter" + exe)],
        "tbmath_filter")

    # P20's first code, Tuesday 27 October 2026, and it is the part of chapter
    # 20 that decides the chapter without a kernel: the latency histogram, the
    # percentile ranks and the verdict against table 20.4's three budget rows.
    # Integer arithmetic throughout, so no contraction flag applies. The ranks
    # are 64-bit on purpose and the header says why.
    # P19's first host code, Wednesday 28 October 2026, and the first thing in
    # that project that can be compared against anything: criteria 4 and 5 are
    # the alignment arithmetic, which needs no transfer engine and no RM0455.
    # Integer arithmetic throughout, so no contraction flag applies.
    print("P19, the maintenance range, criterion 4 and the region encoding:")
    shared_lib("mpualign", [proj("P19", "c") / "mpualign.c"],
               includes=[proj("P19", "c")])

    print("P20, the latency histogram, its percentiles and the budget verdict:")
    shared_lib("latstat", [proj("P20", "c") / "latstat.c"],
               includes=[proj("P20", "c")])

    # P20's C++ variant, Wednesday 28 October 2026. No contraction flag: every
    # quantity in this project is an integer, and the guard in
    # test_float_builds_disable_contraction.py now reads the header as well as
    # the source, so it agrees rather than merely not noticing.
    print("P20, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         *STATIC,
         "-I", proj("P20", "cpp"),
         proj("P20", "cpp") / "latstat_filter.cpp",
         "-o", BUILD / ("latstat_filter" + exe)],
        "latstat_filter")

    print("P08, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         *STATIC,
         "-I", proj("P08", "cpp"), "-I", proj("P09", "cpp"),
         proj("P08", "cpp") / "node_sm_filter.cpp",
         "-o", BUILD / ("node_sm_filter" + exe)],
        "node_sm_filter")

    print("P08, the node's state machine, with P09's codec linked in:")
    shared_lib("node_sm",
               [proj("P08", "c") / "node_sm.c", C_DIR / "payload" / "payload.c"],
               includes=[proj("P08", "c"), C_DIR / "payload"])

    # -ffp-contract=off on both P06 files, and the reason is the comparison
    # rather than a preference. The witness is floating point, and the
    # contraction of a multiply and an add into one fused instruction is not the
    # same across gcc, g++ and rustc. Turning it off removes the question where
    # it can be removed; the parity test's 1e-12 tolerance covers what remains.
    print("P06, the rate witness, which decides whether a rate claim stands:")
    shared_lib("rate", [proj("P06", "c") / "rate.c"],
               ["-ffp-contract=off"], includes=[proj("P06", "c")])

    print("P06, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         "-ffp-contract=off", *STATIC,
         "-I", proj("P06", "cpp"),
         proj("P06", "cpp") / "rate_filter.cpp",
         "-o", BUILD / ("rate_filter" + exe)],
        "rate_filter")

    # P12's two gates. -ffp-contract=off for the same reason P06 has it: the
    # charge gate's percentages and its reconciliation of a total against the sum
    # of its parts are floating point, and the contraction of a multiply and an
    # add is not the same across gcc, g++ and rustc.
    print("P12, the size and charge gates, which must be able to fail:")
    shared_lib("gates", [proj("P12", "c") / "gates.c"],
               ["-ffp-contract=off"], includes=[proj("P12", "c")])

    print("P12, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         "-ffp-contract=off", *STATIC,
         "-I", proj("P12", "cpp"),
         proj("P12", "cpp") / "gates_filter.cpp",
         "-o", BUILD / ("gates_filter" + exe)],
        "gates_filter")

    print("P03, the attribution, which is the half of it that can be proven:")
    shared_lib("attribute", [proj("P03", "c") / "attribute.c"],
               includes=[proj("P03", "c")])

    print("P03, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         *STATIC,
         "-I", proj("P03", "cpp"),
         proj("P03", "cpp") / "attribute_filter.cpp",
         "-o", BUILD / ("attribute_filter" + exe)],
        "attribute_filter")

    print("P02, the C++ variant as a filter driven by the tests:")
    run([cxx, "-std=c++17", "-O2", *WARNINGS, "-fno-exceptions", "-fno-rtti",
         *STATIC,
         "-I", proj("P02", "cpp"),
         proj("P02", "cpp") / "ring_filter.cpp",
         "-o", BUILD / ("ring_filter" + exe)],
        "ring_filter")

    print("P02, the ring, once per ordering mode so all four can be compared:")
    for mode in (0, 1, 2, 3):
        shared_lib("ring{}".format(mode), [C_DIR / "ring" / "ring.c"],
                   ["-DRING_BARRIER={}".format(mode)], includes=[C_DIR / "ring"])

    print("P02, the property test, one binary per ordering mode:")
    for mode in (0, 1, 2, 3):
        run([cc, "-std=c11", "-O2", *WARNINGS, *STATIC,
             "-DRING_BARRIER={}".format(mode),
             "-I", C_DIR / "ring",
             proj("P02", "c") / "property_test.c", C_DIR / "ring" / "ring.c",
             "-o", BUILD / ("property_test{}{}".format(mode, exe))],
            "property_test{}  RING_BARRIER={}".format(mode, mode))

    print("P02, the two-thread soak:")
    run([cc, "-std=c11", "-O2", *WARNINGS, "-pthread", *STATIC,
         "-I", C_DIR / "ring",
         proj("P02", "c") / "soak_threads.c", C_DIR / "ring" / "ring.c",
         "-o", BUILD / ("soak_threads" + exe)],
        "soak_threads")

    print("\nObject sizes at -Os, for the tables. Published, never ranked:")
    for src, out, std, extra in (
        (C_DIR / "payload" / "payload.c", "payload_c.o", "-std=c11", []),
        (proj("P09", "cpp") / "cpp_encode_only.cpp", "payload_cpp.o",
         "-std=c++17", ["-fno-exceptions", "-fno-rtti"]),
        (C_DIR / "ring" / "ring.c", "ring_c.o", "-std=c11", []),
    ):
        compiler = cc if str(src).endswith(".c") else cxx
        subprocess.run(
            [compiler, std, "-Os", *extra,
             "-I", str(C_DIR / "payload"), "-I", str(C_DIR / "ring"), "-I", str(proj("P09", "cpp")),
             "-c", str(src), "-o", str(BUILD / out)],
            check=True, cwd=ROOT, env=compiler_env(compiler),
        )
        print("    {:<16} {:>6} bytes".format(out, (BUILD / out).stat().st_size))

    print(
        "\nThese are host x86-64 objects and are not the flash cost on the\n"
        "board. The flash figures need arm-none-eabi-size, which this laptop\n"
        "does not have, so every budget table keeps that row as not measured."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
