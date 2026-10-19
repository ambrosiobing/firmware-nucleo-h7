"""The committed console captures, read back and checked against what is claimed.

**Why these tests exist.** Until Monday 19 October 2026 every board figure in this
volume was transcribed prose: the number lived in a README and the capture it came from
lived in somebody's scrollback. P02's page had come to say so in words, that a reader
could not check its figures against the board. Ten captures are committed now, and a
committed capture nothing reads is decoration, so these read them.

**What they assert, and each one is a claim made elsewhere in the repository.** That
every capture carries the build identity that makes it evidence at all, an image size
and a build stamp. That the back end in the filename is the back end in the file, so a
mislabelled capture cannot sit here quietly. That the image sizes in the captures are
the sizes `docs/building.md` publishes in its size table, which is the cross-check that
makes these files load bearing rather than archival. That every timer capture reports
the overrun mode as found and the systick ones do not, because that line belongs to one
back end. That the poll count is the same in all ten, which is the evidence for a
statement the index makes carefully. And that the two captures of identical length are
not the same file, because the next person to notice those two numbers will suspect a
duplicate, as I did.

**What they deliberately do not assert.** Nothing about the sampling rate. Every
criterion in `MEASUREMENT.md` except the first is about the instants at which
conversions happened, and no console line observes an instant; only the MCC 118
witness does. A test here that appeared to bear on criteria 2, 3 or 4 would be
claiming something the data cannot carry.
"""
from __future__ import annotations

import hashlib
import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONSOLE = ROOT / "projects" / "P06-timer-sampling" / "console"

STAMPED_LINE = re.compile(r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} ")
IMAGE = re.compile(r"image\s+(\d+) B of flash")
BUILT = re.compile(r"main\.c built\s+(.+?), which moves")
POLLS = re.compile(r"calibration took (\d+) polls")
OVRMOD = re.compile(r"overrun mode as found\s+(\d+)")
ACQUISITION = re.compile(r"acquisition\s+(\w+)")

# The image sizes docs/building.md publishes for each back end, as the captures
# print them. A SET per back end rather than one value, because a back end has as
# many image sizes as it has had builds: the timer build was 15220 B until the TIM6
# factoring of Monday 19 October 2026 and 15276 B after it, and captures of both are
# evidence. This started as one value each and would have rejected the post-move
# captures, which is the test being wrong rather than the captures.
EXPECTED_IMAGE = {"systick": {"14420"}, "timer": {"15220", "15276"}}


def captures():
    found = sorted(CONSOLE.glob("*.txt"))
    assert found, "no captures in %s" % CONSOLE
    return found


def text_of(path):
    return path.read_bytes().decode("utf-8", "replace").replace("\r\n", "\n")


@pytest.mark.parametrize("path", captures(), ids=lambda p: p.name)
def test_every_capture_carries_its_build_identity(path):
    """A capture without its identity is not evidence, which is this volume's own
    test and the reason scan.py requires --image-bytes."""
    text = text_of(path)
    images = set(IMAGE.findall(text))
    stamps = set(BUILT.findall(text))
    assert len(images) == 1, "expected one image size, found %s" % sorted(images)
    assert len(stamps) == 1, "expected one build stamp, found %s" % sorted(stamps)


@pytest.mark.parametrize("path", captures(), ids=lambda p: p.name)
def test_the_back_end_in_the_name_is_the_back_end_in_the_file(path):
    """A mislabelled capture would otherwise sit here quietly, and the filename is
    what a reader cites."""
    text = text_of(path)
    named = "systick" if "systick" in path.name else "timer"
    reported = set(ACQUISITION.findall(text))
    assert reported == {named}, "%s reports %s" % (path.name, sorted(reported))
    sizes = set(IMAGE.findall(text))
    assert sizes <= EXPECTED_IMAGE[named], (
        "%s reports image size %s, which is not a published size for the %s back "
        "end. Either the capture is mislabelled or docs/building.md has a build "
        "this test does not know about" % (path.name, sorted(sizes), named))


def test_the_captured_image_sizes_are_the_published_ones():
    """The cross-check that makes these load bearing. docs/building.md writes the
    sizes with a space as a thousands separator, so the digits are compared."""
    published = (ROOT / "docs" / "building.md").read_text(encoding="utf-8")
    digits = re.sub(r"[^\d]", "", published)
    for backend, sizes in sorted(EXPECTED_IMAGE.items()):
        for size in sorted(sizes):
            assert size in digits, (
                "%s's captured image size %s appears nowhere in docs/building.md, "
                "so either the size table or the captures have moved"
                % (backend, size))


@pytest.mark.parametrize("path", captures(), ids=lambda p: p.name)
def test_the_overrun_mode_line_belongs_to_the_timer_back_end(path):
    """acq_timer.c prints it; acq_systick.c does not. A systick capture that
    reported it, or a timer capture that did not, would mean the line moved."""
    text = text_of(path)
    found = set(OVRMOD.findall(text))
    if "systick" in path.name:
        assert not found, "a systick capture reported the overrun mode: %s" % found
    else:
        assert found == {"0"}, "expected the overrun mode as found 0, got %s" % found


def test_every_capture_reports_the_same_calibration_poll_count():
    """The index says the 187 reading this volume's prose cites is in none of these,
    and this is the half of that statement a test can hold: all ten agree."""
    counts = {}
    for path in captures():
        counts[path.name] = set(POLLS.findall(text_of(path)))
    every = set().union(*counts.values())
    assert every == {"180"}, "poll counts across the captures: %s" % counts


def test_the_two_captures_of_identical_length_are_not_the_same_file():
    """56 675 bytes twice, a week apart, is the shape of a duplicate. It is not one:
    both runs emitted 701 lines of the same shapes and reached seq 10005, and every
    line carries a fixed-width timestamp, so equal totals are what identical run
    shapes produce."""
    by_size = {}
    for path in captures():
        by_size.setdefault(path.stat().st_size, []).append(path)
    for size, paths in sorted(by_size.items()):
        if len(paths) < 2:
            continue
        digests = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        assert len(set(digests.values())) == len(paths), (
            "two captures of %d bytes have the same contents: %s" % (size, digests))
