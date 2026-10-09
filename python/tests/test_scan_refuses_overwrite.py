"""scan.py must refuse to overwrite a capture.

**The loss this pins.** On Friday 9 October 2026 a second 60 second run on the
Raspberry Pi reused `--out ~/run-003` and `scan.py` silently replaced the first,
which held the only interval more than 50 us off nominal seen that day, a 659.94 us
one with one missing edge counted. It survives in a pasted report and nowhere else.
A capture is evidence; a tool that replaces one on a reused name destroys evidence
without saying so, which is the same class of defect as a test that loads a stale
binary as current.

**The shape of the test.** `daqhats` is imported inside `capture()`, so `main` can
be imported here, on a laptop with no HAT, and driven up to the line before the
HAT would be opened. An existing `.csv` or `.json` beside the requested name must
end the run with a message naming the file, before anything is captured. A fresh
name must get past the refusal, which here means reaching `capture()` and failing
there for want of the library, so the test also proves the refusal is not simply
"always exit". `tempfile.mkdtemp` rather than pytest's `tmp_path`, whose
`pytest-current` symlink win11 aquamarine refuses.
"""
from __future__ import annotations

import pathlib
import shutil
import sys
import tempfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "projects" / "P06-timer-sampling" / "python"))

import scan  # noqa: E402


@pytest.fixture
def tmp_dir():
    d = pathlib.Path(tempfile.mkdtemp(prefix="scan-refuse-"))
    try:
        yield d
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_an_existing_csv_is_refused_by_name(tmp_dir):
    out = tmp_dir / "run-003"
    out.with_suffix(".csv").write_text("0.0\n", encoding="utf-8")
    with pytest.raises(SystemExit) as stop:
        scan.main(["--seconds", "1", "--build", "timer", "--out", str(out)])
    assert "refusing to overwrite" in str(stop.value)
    assert "run-003.csv" in str(stop.value)


def test_an_existing_json_alone_is_refused_too(tmp_dir):
    out = tmp_dir / "run-003"
    out.with_suffix(".json").write_text("{}\n", encoding="utf-8")
    with pytest.raises(SystemExit) as stop:
        scan.main(["--seconds", "1", "--build", "timer", "--out", str(out)])
    assert "run-003.json" in str(stop.value)


def test_a_fresh_name_gets_past_the_refusal_to_the_capture(tmp_dir):
    """Reaching capture() on this laptop fails for want of daqhats, and that
    failure is the proof the refusal did not fire: it is a different exception
    with a different message."""
    out = tmp_dir / "run-004"
    with pytest.raises(SystemExit) as stop:
        scan.main(["--seconds", "1", "--build", "timer", "--out", str(out)])
    message = str(stop.value)
    assert "refusing to overwrite" not in message
    # capture()'s own refusal, which means the overwrite check was passed and the
    # run got as far as a laptop with no HAT can take it.
    assert "daqhats is not installed" in message
