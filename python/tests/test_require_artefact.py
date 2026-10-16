"""require_artefact: skip where nothing builds, fail where a build ran and drifted.

**The two machines this separates.** win11 aquamarine never compiles, so it has no
`build-host`; a missing artefact there is expected and the test skips naming the WSL
command. CI and WSL on win11 skyhorizon run `build_host.py` first, so `build-host`
exists; an artefact missing from it is a drifted build and must fail, or a renamed
library would turn a whole file green by never running it. Until Friday 16 October
2026 five loaders failed and the rest skipped regardless of which machine they were
on, which gave the authoring laptop 63 failures for want of a compiler.

The helper reads the module-level BUILD, so the test points BUILD at a temporary
directory for its duration and puts it back in a finally. `tempfile.mkdtemp` rather
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
    saved = conftest.BUILD
    conftest.BUILD = d / "build-host"
    try:
        yield conftest.BUILD
    finally:
        conftest.BUILD = saved
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
