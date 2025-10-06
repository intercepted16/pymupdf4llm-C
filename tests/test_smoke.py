"""Smoke tests for basic functionality."""

import pytest

from pymupdf4llm_c.config import ConversionConfig
from pymupdf4llm_c.models import PageParameters


@pytest.mark.smoke
def test_config_defaults():
    """Test that config has sensible defaults."""
    config = ConversionConfig()
    assert config.dpi == 150
    assert config.table_strategy == "lines_strict"
    assert config.batch_size == 16


@pytest.mark.smoke
def test_page_parameters_init():
    """Test that PageParameters initializes correctly."""
    params = PageParameters()
    assert params.images == []
    assert params.tables == []
    assert params.md_string == ""


@pytest.mark.smoke
def test_config_env_override(tmp_path, monkeypatch):
    """Config should prefer the environment override when locating the library."""
    dummy_lib = tmp_path / "libtomd.so"
    dummy_lib.touch()

    monkeypatch.setenv("PYMUPDF4LLM_C_LIB", str(dummy_lib))

    # Reset any previously cached lookup results so the override is honoured.
    from pymupdf4llm_c._lib import clear_cached_library_path

    clear_cached_library_path()

    config = ConversionConfig()
    resolved = config.resolve_lib_path()

    assert resolved is not None
    assert resolved == dummy_lib.resolve()
