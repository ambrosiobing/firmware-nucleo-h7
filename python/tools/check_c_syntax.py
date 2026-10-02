"""Syntax-check every C file that can be checked on the authoring laptop.

WHY THIS EXISTS. On Saturday 3 October 2026 a patch wrote real carriage returns
into c/P01/main.c instead of the two characters a C string needs, producing five
unterminated string literals. That reached the build laptop, failed there, and
cost a round trip. It would have been caught here in under a second.

The authoring laptop has no cross toolchain, which had been taken since Thursday
1 October 2026 as meaning no C could be checked locally at all. That was wrong: a host gcc ships with Qt at
C:\\Qt\\Tools\\mingw1310_64, and `gcc -fsyntax-only` neither links nor generates
code, so it does not care that the target is a Cortex-M7. It parses, resolves
includes, and type-checks. That catches the whole class of defect that costs a
round trip: unterminated strings, unbalanced braces, misspelled identifiers,
wrong argument counts, missing declarations.

WHAT IT CANNOT CHECK, and the distinction matters so nobody reads a pass here as
a build. It does not generate code, so nothing about size, alignment, register
allocation or the ARM backend is exercised. It uses the host's own stdint and
stddef rather than newlib's. And it skips any file including the vendor device
header, stm32h7xx.h, which is not on this laptop by design. A pass here means the
file is valid C; only the cross build says it is correct firmware.

    python python/tools/check_c_syntax.py
"""

import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Host compilers, in the order they are preferred. The Qt ones are not on PATH,
# which is why they are named in full rather than looked up.
CANDIDATES = [
    r"C:\Qt\Tools\mingw1310_64\bin\gcc.exe",
    r"C:\Qt\Tools\mingw1120_64\bin\gcc.exe",
    "gcc",
    "cc",
]

# Directories to check, and the include paths each needs.
INCLUDE_DIRS = ["c/board", "c/payload", "c/ring", "c/instr", "c/P06"]

# A file including this needs the vendor pack, which is deliberately absent here.
VENDOR_HEADER = re.compile(r'^\s*#\s*include\s*[<"]stm32h7xx\.h[>"]', re.M)

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
    out = []
    for root, dirs, files in os.walk(os.path.join(REPO, "c")):
        dirs[:] = [d for d in dirs if d not in (".git", "build", "build-fw")]
        for f in sorted(files):
            if f.endswith(".c"):
                out.append(os.path.join(root, f))
    return sorted(out)


def main():
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

        if VENDOR_HEADER.search(text):
            print("  skip    %s  (needs stm32h7xx.h, absent by design)" % rel)
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
