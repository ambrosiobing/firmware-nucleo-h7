"""The suite must refuse to run against host artefacts older than their sources.

**The blind spot this pins.** Every host filter and shared library is built by a
command the tests never run, and a test loads whichever file it finds. On Friday
9 October 2026 a `git pull` in WSL on win11 skyhorizon changed `rate.c`, `rate.hpp`
and `lib.rs`; the suite loaded the binaries built from the old ones and reported
"Python says edges=99 and C says 49" as a disagreement between four languages. It
was one new source against three stale binaries. The guard in `conftest.py` now
stops the session with the artefact, the source and the build command named.

**The shape of the test.** A temporary tree with one C source and one artefact,
timestamps set by hand so the test does not depend on how fast the disk is: the
artefact newer than the source is fresh, the source newer than the artefact is
stale, and a missing build directory is nothing at all, which is win11
aquamarine's case every day. `tempfile.mkdtemp` rather than pytest's `tmp_path`,
whose `pytest-current` symlink win11 aquamarine refuses.
"""
from __future__ import annotations

import os
import pathlib
import shutil
import tempfile

import pytest

import conftest


@pytest.fixture
def tree():
    d = pathlib.Path(tempfile.mkdtemp(prefix="stale-guard-"))
    try:
        (d / "c").mkdir()
        (d / "projects").mkdir()
        (d / "python" / "tools").mkdir(parents=True)
        src = d / "c" / "thing.c"
        src.write_text("int x;\n", encoding="utf-8")
        build = d / "build-host"
        build.mkdir()
        art = build / "libthing.so"
        art.write_bytes(b"\x7fELF")
        yield d, src, art, build
    finally:
        shutil.rmtree(d, ignore_errors=True)


def _set_time(p: pathlib.Path, t: float) -> None:
    os.utime(p, (t, t))


def test_an_artefact_newer_than_every_source_is_fresh(tree):
    d, src, art, build = tree
    _set_time(src, 1_000_000.0)
    _set_time(art, 1_000_100.0)
    assert conftest.stale_artefacts(root=d, build=build, rust_filters=[]) == []


def test_a_source_newer_than_the_artefact_names_both_and_the_command(tree):
    d, src, art, build = tree
    _set_time(art, 1_000_000.0)
    _set_time(src, 1_000_100.0)
    stale = conftest.stale_artefacts(root=d, build=build, rust_filters=[])
    assert [(a.name, s.name, c) for a, s, c in stale] == [
        ("libthing.so", "thing.c", "python python/tools/build_host.py")]


def test_no_build_directory_is_nothing_to_report(tree):
    d, src, art, build = tree
    shutil.rmtree(build)
    assert conftest.stale_artefacts(root=d, build=build, rust_filters=[]) == []


def test_a_rust_filter_older_than_a_rust_source_is_stale(tree):
    d, src, art, build = tree
    rs_dir = d / "projects" / "P06" / "rust" / "src"
    rs_dir.mkdir(parents=True)
    rs = rs_dir / "lib.rs"
    rs.write_text("pub fn f() {}\n", encoding="utf-8")
    target = d / "target" / "release"
    target.mkdir(parents=True)
    filt = target / "p06-filter"
    filt.write_bytes(b"\x7fELF")
    _set_time(filt, 1_000_000.0)
    _set_time(rs, 1_000_100.0)
    _set_time(src, 900_000.0)
    _set_time(art, 1_000_000.0)
    stale = conftest.stale_artefacts(root=d, build=build, rust_filters=[filt])
    assert [(a.name, s.name, c) for a, s, c in stale] == [
        ("p06-filter", "lib.rs", "cargo build --release --workspace")]


def test_a_filter_is_not_stale_because_another_project_changed(tree):
    """The false positive the per-crate scoping removes.

    cargo is incremental: `cargo build --release --workspace` relinks only the
    crates whose inputs changed. On Friday 9 October 2026 a pull updated P06's
    lib.rs, cargo rebuilt only p06-filter, and the first guard compared every
    filter against the newest Rust source anywhere, so p01-filter and five others
    were flagged against a source they are not built from, and the printed fix
    could not clear them. p01-filter must be judged against P01's own crate.
    """
    d, src, art, build = tree
    for proj, rs_mtime in (("P01", 1_000_000.0), ("P06", 1_000_100.0)):
        rs_dir = d / "projects" / proj / "rust" / "src"
        rs_dir.mkdir(parents=True)
        rs = rs_dir / "lib.rs"
        rs.write_text("pub fn f() {}\n", encoding="utf-8")
        _set_time(rs, rs_mtime)
    target = d / "target" / "release"
    target.mkdir(parents=True)
    p01 = target / "p01-filter"
    p01.write_bytes(b"ELF")
    _set_time(p01, 1_000_050.0)        # newer than P01's source, older than P06's
    _set_time(src, 900_000.0)
    _set_time(art, 1_000_000.0)
    assert conftest.stale_artefacts(root=d, build=build, rust_filters=[p01]) == []


def test_a_root_manifest_newer_than_a_filter_is_not_staleness(tree):
    """The second false positive, and the rule it established.

    Every condition this guard reports must be one its printed remedy clears. On
    Friday 9 October 2026 five filters were reported older than the root
    Cargo.toml; cargo considers that manifest non-invalidating for them, so
    `cargo build --release --workspace` left them untouched and the session stayed
    blocked with no way forward. Cargo decides what a manifest or lock change
    invalidates and rebuilds exactly that, so the guard compares .rs only.
    """
    d, src, art, build = tree
    rs_dir = d / "projects" / "P05" / "rust" / "src"
    rs_dir.mkdir(parents=True)
    rs = rs_dir / "lib.rs"
    rs.write_text("pub fn f() {}\n", encoding="utf-8")
    _set_time(rs, 1_000_000.0)
    target = d / "target" / "release"
    target.mkdir(parents=True)
    filt = target / "p05-filter"
    filt.write_bytes(b"ELF")
    _set_time(filt, 1_000_050.0)       # newer than its own source
    for name in ("Cargo.toml", "Cargo.lock", "rust-toolchain.toml"):
        f = d / name
        f.write_text("[workspace]\n", encoding="utf-8")
        _set_time(f, 1_000_900.0)      # newer than the filter, and not its business
    _set_time(src, 900_000.0)
    _set_time(art, 1_000_000.0)
    assert conftest.stale_artefacts(root=d, build=build, rust_filters=[filt]) == []
