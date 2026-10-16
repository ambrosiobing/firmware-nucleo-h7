"""scan.py must refuse a capture that would carry no firmware identity.

**Why this is a refusal and not a note.** The board is shared between two volumes, so
the image on it is whatever the last session flashed, and on the Raspberry Pi the only
thing that identifies it is the flash figure the banner printed at the reflash. A
capture without it is attributable only by a stamped console file for the same window,
which is indirect and which the operator may not have taken: on Friday 16 October 2026
two captures were taken that way and the console file had to be found afterwards to
place them. `scan.py`'s own docstring already said of its metadata that without it the
capture is not evidence.

**The gate is before the capture, and one test exists to prove that.** Sixty seconds
of a shared board's time and six million samples are the irreversible part. Refusing
afterwards would cost the run, so the refusal must fire before the HAT is opened. On a
laptop with no `daqhats` installed the two cases are distinguishable by their messages:
the identity refusal if the gate came first, the library's refusal if it did not.

`tempfile.mkdtemp` rather than pytest's `tmp_path`, whose `pytest-current` symlink
win11 aquamarine refuses, as `test_rate_parity.py` has recorded since Sunday
4 October 2026.
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
    d = pathlib.Path(tempfile.mkdtemp(prefix="scan-identity-"))
    try:
        yield d
    finally:
        shutil.rmtree(d, ignore_errors=True)


def _run(tmp_dir, *extra):
    argv = ["--seconds", "1", "--build", "timer", "--out", str(tmp_dir / "run"), *extra]
    with pytest.raises(SystemExit) as stop:
        scan.main(argv)
    return str(stop.value)


def test_no_identity_and_no_waiver_is_refused_by_name(tmp_dir):
    message = _run(tmp_dir)
    assert "no firmware identity" in message
    assert "--image-bytes" in message
    assert "banner" in message


def test_the_refusal_comes_before_the_capture(tmp_dir):
    """The proof that the gate precedes the irreversible step: on this laptop the
    library is absent, so whichever refusal fires names which came first."""
    message = _run(tmp_dir)
    assert "daqhats" not in message, (
        "the capture was attempted before the identity was checked, which on the Pi "
        "would cost sixty seconds of a shared board and six million samples")


def test_a_stated_waiver_gets_past_the_gate(tmp_dir):
    message = _run(tmp_dir, "--no-image-identity", "shakedown of the wiring")
    assert "no firmware identity" not in message
    assert "daqhats is not installed" in message


def test_a_figure_gets_past_the_gate(tmp_dir):
    message = _run(tmp_dir, "--image-bytes", "15220")
    assert "no firmware identity" not in message
    assert "daqhats is not installed" in message


def test_a_slipped_digit_is_refused_with_the_range_named(tmp_dir):
    """Not a verification: 15220 typed for a board running 14420 passes this and is
    caught only by the console file. This catches a figure that is not a size."""
    for absurd in ("152", "15220000"):
        message = _run(tmp_dir, "--image-bytes", absurd)
        assert "not an image size" in message, absurd
        assert "2097152" in message
