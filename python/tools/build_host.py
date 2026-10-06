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
  clocktree.{dll,so}      P01's clock tree decode, the same object the board links
  clocktree_filter        and its C++ variant, as a filter the tests drive
  payload.{dll,so}        P09's codec, from the same source the board links
  cpp_filter              P09's C++ variant
  frame.{dll,so}          P05's COBS, CRC-16 and frame, host and target alike
  frame_filter            P05's C++ variant
  node_sm.{dll,so}        P08's state machine, linking P09's codec
  node_sm_filter          P08's C++ variant
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
