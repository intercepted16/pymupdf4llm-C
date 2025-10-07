# Test Suite Overview

This directory houses smoke, unit, and integration coverage for the `pymupdf4llm_c` package.

## What Lives Here
- `test_smoke.py` – lightweight checks that `ConversionConfig` respects the `PYMUPDF4LLM_C_LIB` override (`@pytest.mark.smoke`).
- `test_conversion.py` – end-to-end exercises of `to_json` that generate synthetic PDFs and assert JSON block structure (`@pytest.mark.integration`, `@pytest.mark.requires_pdf`).
- `pdf_fixtures.py` – ReportLab-based factory for the synthetic PDFs used in conversion tests (tables, headings, formatting, lists).
- `manual_test.py` – tiny helper to try the JSON extractor manually: `python tests/manual_test.py input.pdf output_dir`.
- `baseline/run_baseline.py` – snapshot of the upstream PyMuPDF baseline conversion script kept for comparison/reference.
- `test_data/` – cache of generated fixture PDFs; created on demand and intentionally left in place between runs.

## Running The Tests
1. Install dev dependencies: `pip install -r requirements-test.txt` (also make sure the `pymupdf4llm_c` extension is built or installed locally).
2. Run everything: `pytest` (integration cases expect a working native library and will skip the `nist.pdf` check if the file is missing).
3. Quick smoke check only: `pytest -m smoke`.
4. Skip the native conversion path: `pytest -m "not requires_pdf"`.

The integration tests write temporary per-page JSON outputs under a temp directory and reuse the PDFs in `tests/test_data`. No manual cleanup is required.
