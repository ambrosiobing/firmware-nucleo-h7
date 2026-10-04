"""Every name this repository's Python reads, checked against what binds it.

**Why a check like this exists here and not in most repositories.** Half of this
suite's code paths cannot run on the laptop that writes them. Compiling on win11
aquamarine is forbidden, so the branches that drive a compiled filter are dead
there: `path.exists()` is false, the implementation is reported as absent with
the command and the laptop named, and the test passes. That is the pattern which
makes a green tick on the authoring laptop honest, and on Sunday 4 October 2026
it was also the pattern that hid a defect.

`test_rate_parity.py` called its filter helper with `tmp_path`, a name left over
from a rename and bound nowhere in the module. The line sat in the branch that
runs only when a compiled filter exists, so win11 aquamarine never reached it and
reported five of six parity tables green. The NameError appeared the first time a
laptop had all four implementations, which is the only time the four-language
comparison runs at all.

An undefined name is the one class of defect that is visible without running
anything, so it is worth a check that runs everywhere, including on the laptop
that cannot compile.

**It uses the compiler's own scope analysis and not a guess at one.** The
`symtable` module is what CPython builds before it emits bytecode, so parameters,
comprehension scopes, closures, `global` and `nonlocal`, walrus bindings and
conditional imports are all resolved the way the interpreter resolves them rather
than by a reimplementation here. A name the compiler decided to look up in the
module or in builtins, which is bound in neither, cannot resolve at run time. It
reported nothing across every Python file in this repository on the day it was
written, and it reports the P06 defect above when that line is restored, so it is
neither vacuous nor noisy.

**It is not a type checker and is not a step towards one.** It answers one
question with no configuration and no dependency.
"""
from __future__ import annotations

import builtins
import symtable
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Names every module gets without binding them, beyond the builtins themselves.
MODULE_DUNDERS = {
    "__file__", "__name__", "__doc__", "__package__", "__spec__",
    "__loader__", "__builtins__", "__path__", "__debug__",
}
KNOWN = set(dir(builtins)) | MODULE_DUNDERS

# Where this repository's Python lives. The chapters' code samples are not here
# and are not importable, which is a separate question answered by lint.py.
SEARCH = ("python/tests", "python/tools", "python/firmkit")
ALSO = ("lint.py", "crosscheck.py")


def bound_names(table):
    """The names this scope binds, by the compiler's own account."""
    return {s.get_name() for s in table.get_symbols()
            if s.is_assigned() or s.is_imported() or s.is_parameter()}


def undefined_in(path):
    """Names the module reads, at any depth, that nothing in it binds."""
    source = path.read_text(encoding="utf-8")
    try:
        top = symtable.symtable(source, str(path), "exec")
    except SyntaxError as exc:
        return [("the file does not parse", str(exc), exc.lineno or 0)]

    module = bound_names(top)
    found = []

    def walk(table):
        for symbol in table.get_symbols():
            # is_global() is the compiler's verdict that this name is neither
            # local to the scope nor captured from an enclosing one, so the only
            # places left to find it are the module and builtins.
            if symbol.is_global() and symbol.is_referenced():
                name = symbol.get_name()
                if name not in module and name not in KNOWN:
                    # symtable calls the module scope "top", which is not what a
                    # reader of the message wants to be told.
                    where = ("at module level" if table is top
                             else "in " + table.get_name())
                    found.append((where, name, table.get_lineno()))
        for child in table.get_children():
            walk(child)

    # The module's own scope is walked too, and not only the functions and
    # classes inside it. Checking the children alone missed a whole class of
    # defect, found on Sunday 4 October 2026 while proving this tool can fail: a
    # name read at module level, which is where every one of these scripts calls
    # main() under the usual guard, was never looked at.
    walk(top)
    return found


def files():
    for folder in SEARCH:
        for path in sorted((ROOT / folder).rglob("*.py")):
            if "__pycache__" not in path.parts:
                yield path
    for name in ALSO:
        path = ROOT / name
        if path.is_file():
            yield path


def main():
    checked = 0
    problems = 0
    for path in files():
        checked += 1
        for scope, name, line in undefined_in(path):
            problems += 1
            # symtable gives the module scope line 0, which is not a place.
            where = scope if line == 0 else "{}, near line {}".format(scope, line)
            print("{}: {}: {} is read and nothing binds it"
                  .format(path.relative_to(ROOT).as_posix(), where, name))
    print("{} Python files, every name they read resolves"
          .format(checked) if not problems else
          "{} Python files, {} unresolved".format(checked, problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
