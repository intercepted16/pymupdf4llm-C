"""Public facing API helpers."""
from __future__ import annotations

import logging
from pathlib import Path
from typing import Optional

from .config import ConversionConfig
from .logging_config import get_logger

try:  # pragma: no cover - optional import during bootstrap
    from . import main as _bindings
except ImportError:  # pragma: no cover - optional import during bootstrap
    _bindings = None  # type: ignore[assignment]

_LOGGER = get_logger(__name__)


class ConversionError(RuntimeError):
    """Raised when the underlying conversion pipeline reports a failure."""


def to_markdown(
    pdf_path: str | Path,
    *,
    output_path: str | Path | None = None,
    config: Optional[ConversionConfig] = None,
) -> Path:
    """Convert ``pdf_path`` to Markdown and return the resulting path.

    The function first attempts to leverage the compiled C bindings for
    maximum throughput. If the compiled library cannot be located, a Python
    fallback using :mod:`pymupdf4llm` is used instead.
    """
    config = config or ConversionConfig()

    pdf_path = Path(pdf_path)
    if not pdf_path.exists():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    output_path = Path(output_path) if output_path else pdf_path.with_suffix(".md")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    lib_path = config.resolve_lib_path()
    use_batch = config.use_batch_callback

    if _bindings is not None:
        try:
            if use_batch:
                success = _bindings.convert_pdf_with_batch_callback(
                    str(pdf_path),
                    str(output_path),
                    str(lib_path) if lib_path else None,
                )
            else:
                success = _bindings.convert_pdf_with_callback(
                    str(pdf_path),
                    str(output_path),
                    str(lib_path) if lib_path else None,
                )
        except FileNotFoundError:
            _LOGGER.warning("Compiled library missing, falling back to Python implementation.")
            success = False
        except OSError as exc:
            _LOGGER.warning("Failed to load compiled library (%s), falling back to Python implementation.", exc)
            success = False

        if success:
            return output_path

    _python_fallback(pdf_path, output_path, config)
    return output_path


def _python_fallback(pdf_path: Path, output_path: Path, config: ConversionConfig) -> None:
    """Pure Python implementation of the conversion fallback path."""
    try:
        import pymupdf4llm
    except ImportError as exc:  # pragma: no cover - optional dependency during tests
        raise ConversionError(
            "pymupdf4llm is required for the Python fallback but is not installed."
        ) from exc

    kwargs = {
        "pages": None,
        "page_chunks": False,
        "write_images": False,
        "image_path": "",
        "image_format": "png",
        "dpi": config.dpi,
    }
    kwargs.update(config.pymupdf_kwargs)

    markdown = pymupdf4llm.to_markdown(str(pdf_path), **kwargs)

    if isinstance(markdown, str):
        output_path.write_text(markdown)
    elif markdown is None:
        if not output_path.exists():
            raise ConversionError("pymupdf4llm returned no output and the target file was not created")
    else:
        output_path.write_text(str(markdown))

    _LOGGER.debug("Python fallback completed for %%s", pdf_path)
