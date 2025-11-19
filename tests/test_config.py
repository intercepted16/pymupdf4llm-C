"""Tests covering config and environment handling."""

from __future__ import annotations

from pathlib import Path

import pytest

from pymupdf4llm_c.config import ConversionConfig


@pytest.fixture
def clean_lib_cache():
    """Ensure the library path cache is cleared before and after the test."""
    from pymupdf4llm_c._lib import clear_cached_library_path

    clear_cached_library_path()
    yield
    clear_cached_library_path()


@pytest.mark.smoke
def test_config_default_path_resolves(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, clean_lib_cache
):
    """When no override is provided the config should fall back to discovery."""
    dummy_lib = tmp_path / "libtomd.so"
    dummy_lib.write_text("test")

    monkeypatch.setenv("PYMUPDF4LLM_C_LIB", str(dummy_lib))

    config = ConversionConfig()
    resolved = config.resolve_lib_path()

    assert resolved is not None
    assert resolved.resolve() == dummy_lib.resolve()


@pytest.mark.smoke
def test_config_explicit_path(tmp_path: Path):
    """When an explicit path is provided it should take precedence."""
    explicit = tmp_path / "libtomd.so"
    explicit.write_text("test")

    config = ConversionConfig(lib_path=explicit)
    resolved = config.resolve_lib_path()

    assert resolved is not None
    assert resolved.resolve() == explicit.resolve()
