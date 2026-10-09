"""check_c_syntax must skip a file that reaches stm32h7xx.h through a local header.

**The blind spot this pins.** The checker skips any first-party C file that needs
the vendor device header, because that header is absent here by design. Until
Friday 9 October 2026 it decided that by searching the `.c` file alone. On that day
`adc_bringup.c`, which includes `adc_bringup.h`, which includes `stm32h7xx.h`, was
compiled on the host and reported as the one failure in 40 files, in WSL on win11
skyhorizon, the only place the checker runs. CI never runs this checker, so nothing
else could have said so.

**The shape of the test.** Four small trees in a temporary directory, no compiler
anywhere near them: a direct include, an include through one header, an include
through a header that is only reachable along the include path, and two headers
that include each other and never reach the vendor header. The last proves the walk
terminates. And the first case of the second kind is checked against the bare
regular expression too, so the test also records that the old decision would have
missed it: a test that cannot name the defect it guards against is decoration.
"""
from __future__ import annotations

import pathlib
import shutil
import sys
import tempfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python" / "tools"))

import check_c_syntax as checker  # noqa: E402


@pytest.fixture
def tmp_path():
    """A plain temporary directory, not pytest's.

    pytest's tmp_path keeps a `pytest-current` symlink beside its directories, and
    on win11 aquamarine creating that symlink is refused with WinError 5, so every
    test using the fixture errors before it starts. test_rate_parity.py found that
    on Sunday 4 October 2026 and this file repeats its fix rather than its mistake.
    """
    d = pathlib.Path(tempfile.mkdtemp(prefix="check-c-syntax-"))
    try:
        yield d
    finally:
        shutil.rmtree(d, ignore_errors=True)


def _write(p: pathlib.Path, text: str) -> pathlib.Path:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding="utf-8")
    return p


def test_a_direct_include_is_reported_as_direct(tmp_path):
    c = _write(tmp_path / "a.c", '#include "stm32h7xx.h"\nint x;\n')
    assert checker.vendor_header_via(str(c), c.read_text(), []) == ""


def test_an_include_through_a_local_header_is_followed_and_named(tmp_path):
    _write(tmp_path / "bring.h", '#include <stdint.h>\n#include "stm32h7xx.h"\n')
    c = _write(tmp_path / "bring.c", '#include "bring.h"\nint x;\n')
    text = c.read_text()
    # The old decision, the regular expression on the .c file alone, misses it.
    assert checker.VENDOR_HEADER.search(text) is None
    via = checker.vendor_header_via(str(c), text, [])
    assert via is not None and via.endswith("bring.h")


def test_a_header_found_only_along_the_include_path_is_followed(tmp_path):
    inc = tmp_path / "shared"
    _write(inc / "regs.h", '#include "stm32h7xx.h"\n')
    c = _write(tmp_path / "proj" / "use.c", '#include "regs.h"\nint x;\n')
    assert checker.vendor_header_via(str(c), c.read_text(), []) is None
    via = checker.vendor_header_via(str(c), c.read_text(), [str(inc)])
    assert via is not None and via.endswith("regs.h")


def test_headers_that_include_each_other_terminate_and_are_not_vendor(tmp_path):
    _write(tmp_path / "p.h", '#include "q.h"\n')
    _write(tmp_path / "q.h", '#include "p.h"\n')
    c = _write(tmp_path / "loop.c", '#include "p.h"\nint x;\n')
    assert checker.vendor_header_via(str(c), c.read_text(), []) is None


def test_the_real_file_that_exposed_it_is_now_skipped():
    """adc_bringup.c in the repository, resolved against the real include path."""
    c = ROOT / "projects" / "P06-timer-sampling" / "c" / "adc_bringup.c"
    via = checker.vendor_header_via(str(c), c.read_text(encoding="utf-8"))
    assert via == "projects/P06-timer-sampling/c/adc_bringup.h"
