"""require_artefact: skip where nothing builds, fail where a build ran and drifted.

**The two machines this separates.** win11 aquamarine never compiles, so it has no
`build-host`; a missing artefact there is expected and the test skips naming the WSL
command. CI and WSL on win11 skyhorizon run `build_host.py` first, so `build-host`
exists; an artefact missing from it is a drifted build and must fail, or a renamed
library would turn a whole file green by never running it. Until Friday 16 October
2026 five loaders failed and the rest skipped regardless of which machine they were
on, which gave the authoring laptop 63 failures for want of a compiler.

The helper reads the artefact's own parent directory, `build-host` for what
build_host.py makes and `target/release` for what cargo makes, so one rule serves
both and the test exercises both on a temporary tree. `tempfile.mkdtemp` rather
than pytest's `tmp_path`, whose `pytest-current` symlink win11 aquamarine refuses.
"""
from __future__ import annotations

import pathlib
import shutil
import tempfile

import pytest

import conftest


@pytest.fixture
def build_dir():
    d = pathlib.Path(tempfile.mkdtemp(prefix="require-artefact-"))
    try:
        yield d / "build-host"
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_a_present_artefact_returns_quietly(build_dir):
    build_dir.mkdir()
    lib = build_dir / "libthing.so"
    lib.write_bytes(b"ELF")
    assert conftest.require_artefact(lib) is None


def test_no_build_directory_at_all_skips_and_names_the_command(build_dir):
    assert not build_dir.exists()
    with pytest.raises(pytest.skip.Exception) as why:
        conftest.require_artefact(build_dir / "libthing.so", "python3 python/tools/build_host.py")
    assert "does not compile" in str(why.value)
    assert "python3 python/tools/build_host.py" in str(why.value)


def test_a_build_directory_without_the_artefact_fails_as_drift(build_dir):
    build_dir.mkdir()
    (build_dir / "libother.so").write_bytes(b"ELF")
    with pytest.raises(pytest.fail.Exception) as why:
        conftest.require_artefact(build_dir / "libthing.so")
    assert "drifted" in str(why.value)
    assert "libthing.so" in str(why.value)


def test_a_rust_filter_is_judged_by_target_release_the_same_way(build_dir):
    """cargo's output lives under target/release, not build-host; the rule reads
    the artefact's own directory, so no second argument is needed."""
    release = build_dir.parent / "target" / "release"
    with pytest.raises(pytest.skip.Exception):
        conftest.require_artefact(release / "p06-filter", "cargo build --release --workspace")
    release.mkdir(parents=True)
    with pytest.raises(pytest.fail.Exception) as why:
        conftest.require_artefact(release / "p06-filter", "cargo build --release --workspace")
    assert "release exists" in str(why.value)


def test_required_fails_even_where_nothing_builds(build_dir):
    """The one test per project whose purpose is to notice that nothing was
    compared must fail on a missing artefact on every machine."""
    with pytest.raises(pytest.fail.Exception) as why:
        conftest.require_artefact(build_dir / "libthing.so", required=True)
    assert "requires it" in str(why.value)
