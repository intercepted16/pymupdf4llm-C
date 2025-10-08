"""Smoke tests covering config and environment handling."""

from __future__ import annotations

from pathlib import Path

import pytest

from pymupdf4llm_c.config import ConversionConfig


@pytest.mark.smoke
def test_config_default_path_resolves(monkeypatch, tmp_path: Path):
    """When no override is provided the config should fall back to discovery."""
    dummy_lib = tmp_path / "libtomd.so"
    dummy_lib.write_text("test")

    monkeypatch.setenv("PYMUPDF4LLM_C_LIB", str(dummy_lib))

    from pymupdf4llm_c._lib import clear_cached_library_path

    clear_cached_library_path()

    config = ConversionConfig()
    resolved = config.resolve_lib_path()

    assert resolved is not None
    assert resolved.resolve() == dummy_lib.resolve()


@pytest.mark.smoke
def test_config_explicit_path(tmp_path: Path):
    explicit = tmp_path / "libtomd.so"
    explicit.write_text("test")

    config = ConversionConfig(lib_path=explicit)
    resolved = config.resolve_lib_path()

    assert resolved is not None
    assert resolved.resolve() == explicit.resolve()
