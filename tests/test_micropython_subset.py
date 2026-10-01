"""Keep codec/payload.py inside the MicroPython subset, by test rather than by hope.

The chapter's claim is that one decoder source serves the host and the target:
the same file runs under MicroPython on the board. That claim costs nothing to
make and rots silently, because every change to payload.py is tested on CPython
where f-strings and annotations work fine.

So the constraints are asserted here. This does not prove the file runs on the
board, which needs the board; it proves the file has not drifted out of the
subset since the last time somebody checked that it did.

The two constraints, from the chapter:
  - nothing outside the interpreter's subset, which for this module means no
    f-strings in the hot path and no annotations needing a module the firmware
    does not carry
  - no imports beyond the generated field table
"""
from __future__ import annotations

import ast

from conftest import ROOT

SOURCE = ROOT / "firmkit" / "payload.py"


def tree():
    return ast.parse(SOURCE.read_text(encoding="utf-8"), filename=str(SOURCE))


def test_no_f_strings():
    found = [
        node.lineno for node in ast.walk(tree()) if isinstance(node, ast.JoinedStr)
    ]
    assert not found, "f-strings at lines {} are outside the subset".format(found)


def test_no_annotations():
    bad = []
    for node in ast.walk(tree()):
        if isinstance(node, ast.AnnAssign):
            bad.append(node.lineno)
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if node.returns is not None:
                bad.append(node.lineno)
            for arg in list(node.args.args) + list(node.args.kwonlyargs):
                if arg.annotation is not None:
                    bad.append(node.lineno)
    assert not bad, "annotations at lines {} need typing on the board".format(sorted(set(bad)))


def test_imports_only_the_generated_field_table():
    allowed = {"payload_fields"}
    imported = set()
    for node in ast.walk(tree()):
        if isinstance(node, ast.Import):
            imported.update(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            if node.module:
                imported.add(node.module)
    assert imported <= allowed, "unexpected imports: {}".format(sorted(imported - allowed))


def test_no_future_import():
    """`from __future__ import annotations` does not exist on the board."""
    for node in ast.walk(tree()):
        if isinstance(node, ast.ImportFrom) and node.module == "__future__":
            raise AssertionError(
                "a __future__ import at line {} will not load on the board".format(
                    node.lineno
                )
            )


def test_the_decoder_is_importable_with_only_the_field_table_present():
    """A crude stand-in for the board: import it with nothing else available.

    If payload.py gained a dependency on a module the firmware does not carry,
    this would be the first test to notice, and it needs no board to run.
    """
    import importlib
    import sys

    for name in ("payload", "payload_fields"):
        sys.modules.pop(name, None)
    module = importlib.import_module("payload")
    assert module.decode(bytes.fromhex("2405FFFFE8"))["feature"] == -1
