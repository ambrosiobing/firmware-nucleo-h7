"""Syntax-check every C file that can be checked. RUNS IN WSL ON SKYHORIZON ONLY.

    bing@JPTOUPM678:~$ python3 python/tools/check_c_syntax.py

WHY THIS EXISTS. On Saturday 3 October 2026 a patch wrote real carriage returns
into projects/P01-toolchain-first-light/c/main.c instead of the two characters a C string needs, producing five
unterminated string literals. That reached the build laptop, failed there, and
cost a round trip. `gcc -fsyntax-only` neither links nor generates code, so it
does not care that the target is a Cortex-M7: it parses, resolves includes and
type-checks, which catches that whole class of defect in under a second.

WHERE IT MAY RUN, and this is a rule rather than a preference. **Compiling
happens in WSL on the win11 skyhorizon demo laptop, `bing@JPTOUPM678`, and
nowhere else.** Stated by Joseph on Saturday 3 October 2026. This script refuses
to run on Windows, and that refusal is the point: a host compiler on the
authoring laptop answers a question about the host, and an answer that looks like
a build invites the conclusion that the firmware is fine.

An earlier version of this docstring argued the opposite, that a host gcc ships
with Qt on the authoring laptop and may as well be used. That reasoning was
sound and the policy overrules it.

IT ALSO DID NOT WORK THERE, which is worth recording so nobody reinstates it.
Run from PowerShell on the authoring laptop it reported every one of nineteen
files as failing, including files that include almost nothing and cannot all be
broken. The cause was not the code. `gcc.exe` loads because its DLLs sit beside
it, so `-dumpversion` succeeds and the banner prints a version; the real compiler
is `cc1.exe` under libexec, which needs libwinpthread-1.dll and
libgcc_s_seh-1.dll from the compiler's own bin directory, and without that
directory on PATH it dies before writing anything. The same script passed from a
shell that happened to carry a compatible mingw on PATH. A tool whose result
depends on which shell started it is not a check, and in WSL the question does
not arise.

WHAT IT CANNOT CHECK, so nobody reads a pass here as a build. It does not
generate code, so nothing about size, alignment, register allocation or the ARM
backend is exercised. It uses the host's own stdint and stddef rather than
newlib's. And it skips any file that includes the vendor device header,
stm32h7xx.h, directly or through a local header it includes: adc_bringup.c
reaches it through adc_bringup.h, and until Friday 9 October 2026 this looked
only at the .c file, so that file was compiled against a header that is absent
by design and reported as failing. A pass here means the file is valid C; only
the cross build says it
is correct firmware.
"""

import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Host compilers, in the order they are preferred. The Qt ones are not on PATH,
# which is why they are named in full rather than looked up.
CANDIDATES = ["gcc", "cc"]
# Plain names, because this runs in WSL where the compiler is on PATH and is an
# ordinary Linux gcc. The two Qt mingw paths that used to head this list were
# removed on Saturday 3 October 2026: naming a Windows compiler here invited
# running one, and that is the thing this script must not do.

# Directories to check, and the include paths each needs.
INCLUDE_DIRS = ["c/board", "c/clock", "c/payload", "c/ring", "c/instr",
                "projects/P06-timer-sampling/c", "projects/P03-interrupt-receive/c",
                "projects/P05-framing-crc/c", "projects/P08-node-state-machine/c",
                "projects/P09-payload-codec/cpp"]

# A file including this needs the vendor pack, which is deliberately absent here.
VENDOR_HEADER = re.compile(r'^\s*#\s*include\s*[<"]stm32h7xx\.h[>"]', re.M)

# A quoted include, which the compiler resolves beside the including file first
# and then along the include path; the angle-bracket form is left to the system.
LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def vendor_header_via(path, text, include_dirs=None):
    """How `path` reaches stm32h7xx.h, or None if it does not.

    Returns "" when the file includes it directly, otherwise the path of the
    first local header through which it is reached, relative to the repository,
    so the skip line can say which. Quoted includes are followed the way the
    compiler follows them, beside the including file and then along
    INCLUDE_DIRS, each header read once, so a header that includes itself or
    two that include each other terminate. Looking at the .c file alone was
    the fourth blind spot found in this repository's checkers in one week.
    """
    if include_dirs is None:
        include_dirs = [os.path.join(REPO, d) for d in INCLUDE_DIRS]
    if VENDOR_HEADER.search(text):
        return ""
    seen = set()
    stack = [(os.path.dirname(path), name) for name in LOCAL_INCLUDE.findall(text)]
    while stack:
        here, name = stack.pop(0)
        for d in [here] + list(include_dirs):
            cand = os.path.normpath(os.path.join(d, name))
            if os.path.isfile(cand):
                break
        else:
            continue
        if cand in seen:
            continue
        seen.add(cand)
        try:
            htext = open(cand, encoding="utf-8").read()
        except (OSError, UnicodeDecodeError):
            continue
        if VENDOR_HEADER.search(htext):
            return os.path.relpath(cand, REPO).replace("\\", "/")
        stack.extend((os.path.dirname(cand), n) for n in LOCAL_INCLUDE.findall(htext))
    return None

# Files that define newlib's syscall hooks cannot be checked by a host compiler,
# and this is a limitation of the method rather than a defect in the code. On ARM
# newlib the signature is int _write(int, char *, int). Windows declares _write in
# its own io.h as int _write(int, const void *, unsigned int) for the CRT, so the
# host sees a conflict that does not exist on the target. Found on Saturday
# 3 October 2026, on the first run of this tool, which is a fair result for a tool
# written to catch false confidence.
SYSCALL_DEF = re.compile(
    r'^\s*(?:int|void|caddr_t|void\s*\*|ssize_t)\s+'
    r'_(write|read|close|lseek|isatty|fstat|sbrk|exit|kill|getpid)\s*\(', re.M)


def find_compiler():
    for c in CANDIDATES:
        if os.path.isabs(c):
            if os.path.isfile(c):
                return c
        else:
            found = shutil.which(c)
            if found:
                return found
    return None


def c_files():
    """Every first-party C file: the shared board support under c/, and each
    project's own c/ and cpp/ subdirectory. Both trees are walked because a
    project owns its sources and c/ holds only what more than one project uses,
    so checking one tree would leave most of the volume unchecked."""
    out = []
    roots = [os.path.join(REPO, "c")]
    projects = os.path.join(REPO, "projects")
    if os.path.isdir(projects):
        for name in sorted(os.listdir(projects)):
            for lang in ("c", "cpp"):
                d = os.path.join(projects, name, lang)
                if os.path.isdir(d):
                    roots.append(d)
    for top in roots:
        for root, dirs, files in os.walk(top):
            dirs[:] = [d for d in dirs
                       if d not in (".git", "build", "build-fw", "__pycache__")]
            for f in sorted(files):
                if f.endswith(".c"):
                    out.append(os.path.join(root, f))
    return sorted(out)


def main():
    # The rule, enforced rather than written in the docstring and hoped for.
    # Compiling happens in WSL on win11 skyhorizon, bing@JPTOUPM678, and nowhere
    # else. Refusing here is not an inconvenience to work around: see the
    # docstring for why the Windows path both should not and did not work.
    if os.name == "nt" or sys.platform.startswith("win"):
        print("This runs in WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678,")
        print("and nowhere else. Compiling does not happen on the authoring laptop.")
        print()
        print("  bing@JPTOUPM678:~$ cd <repo> ; python3 python/tools/check_c_syntax.py")
        print()
        print("Nothing was checked. This is not a pass, and it is not a failure of")
        print("the code either: no compiler was run.")
        return 2

    gcc = find_compiler()
    if gcc is None:
        print("no host C compiler found. Looked for:")
        for c in CANDIDATES:
            print("   ", c)
        print("\nNothing is checked. This is not a pass.")
        return 2

    print("compiler: %s" % gcc)
    ver = subprocess.run([gcc, "-dumpversion"], capture_output=True, text=True)
    print("version:  %s" % ver.stdout.strip())
    print()

    args = [gcc, "-fsyntax-only", "-std=c11", "-Wall", "-Wextra"]
    for d in INCLUDE_DIRS:
        args += ["-I", os.path.join(REPO, d)]

    checked, skipped, failed = 0, 0, 0
    for path in c_files():
        rel = os.path.relpath(path, REPO).replace("\\", "/")
        try:
            text = open(path, encoding="utf-8").read()
        except UnicodeDecodeError:
            print("  SKIP    %s  (not UTF-8)" % rel)
            skipped += 1
            continue

        via = vendor_header_via(path, text)
        if via is not None:
            how = "" if via == "" else " through %s" % via
            print("  skip    %s  (needs stm32h7xx.h%s, absent by design)" % (rel, how))
            skipped += 1
            continue

        if SYSCALL_DEF.search(text):
            print("  skip    %s  (defines newlib syscalls; the host CRT declares"
                  " the same names with different signatures)" % rel)
            skipped += 1
            continue

        # A raw carriage return inside a source file is never intended here and is
        # the exact defect this tool was written for, so it is reported as such
        # rather than left for the compiler to describe obliquely.
        if "\r" in text:
            n = text[: text.index("\r")].count("\n") + 1
            print("  FAIL    %s  raw carriage return at line %d" % (rel, n))
            failed += 1
            continue

        r = subprocess.run(args + [path], capture_output=True, text=True)
        if r.returncode == 0:
            print("  ok      %s" % rel)
            checked += 1
        else:
            print("  FAIL    %s" % rel)
            for line in (r.stderr or r.stdout).splitlines()[:8]:
                print("              %s" % line)
            failed += 1

    print()
    print("  %d checked, %d skipped, %d failed" % (checked, skipped, failed))
    if failed:
        print("\n  A failure here is valid-C failure, not a firmware failure.")
        print("  Fix it before pushing: the cross build on the other laptop will")
        print("  report the same thing a round trip later.")
        return 1
    print("\n  Every checkable file is valid C. That is not the same as correct")
    print("  firmware: no code was generated and the ARM backend was not used.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
