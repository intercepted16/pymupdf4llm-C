"""Block to markdown conversion logic."""

from __future__ import annotations

import re
from typing import Any

# Bullet characters to replace with markdown list markers
BULLET_CHARS = frozenset(
    [
        "\u2022",  # • BULLET
        "\u2023",  # ‣ TRIANGULAR BULLET
        "\u2043",  # ⁃ HYPHEN BULLET
        "\u204c",  # ⁌ BLACK LEFTWARDS BULLET
        "\u204d",  # ⁍ BLACK RIGHTWARDS BULLET
        "\u2219",  # ∙ BULLET OPERATOR
        "\u25aa",  # ▪ BLACK SMALL SQUARE
        "\u25ab",  # ▫ WHITE SMALL SQUARE
        "\u25cf",  # ● BLACK CIRCLE
        "\u25cb",  # ○ WHITE CIRCLE
        "\u25e6",  # ◦ WHITE BULLET
        "\u25a0",  # ■ BLACK SQUARE
        "\u25a1",  # □ WHITE SQUARE
        "\u25b6",  # ▶ BLACK RIGHT-POINTING TRIANGLE
        "\u25b8",  # ▸ BLACK RIGHT-POINTING SMALL TRIANGLE
        "\u25c6",  # ◆ BLACK DIAMOND
        "\u25c7",  # ◇ WHITE DIAMOND
        "\u2666",  # ♦ BLACK DIAMOND SUIT
        "\u27a4",  # ➤ BLACK RIGHTWARDS ARROWHEAD
        "\uf0b7",  # Private use - common bullet in PDFs
        "\ufffd",  # � REPLACEMENT CHARACTER (often a bullet)
    ]
)


def _normalize_bullets(text: str) -> str:
    """Replace bullet characters with markdown list marker."""
    result = []
    i = 0
    while i < len(text):
        if text[i] in BULLET_CHARS:
            # Replace bullet with "- " for markdown list
            result.append("- ")
            # Skip any whitespace after the bullet
            i += 1
            while i < len(text) and text[i] in " \t":
                i += 1
        else:
            result.append(text[i])
            i += 1
    return "".join(result)


def _style_span(span: dict[str, Any]) -> str:
    """Apply styling to a single span's text."""
    span_text = span.get("text", "")
    if not span_text:
        return ""

    # Handle superscript - render as bracketed footnote reference if numeric
    if span.get("superscript"):
        if span_text.strip().isdigit() or re.match(r"^\d+[,\s\d]*$", span_text.strip()):
            return f"[{span_text.strip()}]"
        return f"^{span_text}^"

    if span.get("monospace"):
        span_text = f"`{span_text}`"
    if span.get("bold"):
        span_text = f"**{span_text}**"
    if span.get("italic"):
        span_text = f"*{span_text}*"
    if span.get("strikeout"):
        span_text = f"~~{span_text}~~"
    if span.get("subscript"):
        span_text = f"~{span_text}~"
    return span_text


def _join_styled_spans(spans: list[dict[str, Any]]) -> str:
    """Join styled spans with proper spacing around formatting markers."""
    if not spans:
        return ""

    parts = []
    for i, span in enumerate(spans):
        styled = _style_span(span)
        if not styled:
            continue

        # Add space before formatting if needed (avoid "word**bold**" -> "word **bold**")
        if parts and styled.startswith(("**", "*", "`", "~~")):
            prev = parts[-1]
            if prev and not prev.endswith((" ", "\n", "\t", "(", "[", "/")):
                parts.append(" ")

        parts.append(styled)

        # Add space after formatting if needed (avoid "**bold**word" -> "**bold** word")
        # This is handled by checking next span
        if i + 1 < len(spans):
            next_span = spans[i + 1]
            next_text = next_span.get("text", "")
            if (
                styled.endswith(("**", "*", "`", "~~"))
                and next_text
                and not next_text.startswith(
                    (" ", "\n", "\t", ".", ",", ":", ";", ")", "]", "/", "-", "?", "!")
                )
            ):
                parts.append(" ")

    return "".join(parts)


def block_to_markdown(block: dict[str, Any]) -> str:
    """Convert a single block dictionary to markdown string."""
    block_type = block.get("type", "")
    text = block.get("text", "").strip()

    # If no top-level text, extract from spans with styling
    if not text and block.get("spans"):
        text = _join_styled_spans(block.get("spans", []))

    # Normalize bullet characters to markdown list markers
    if text:
        text = _normalize_bullets(text)

    if block_type == "heading" and text:
        return f"{'#' * block.get('level', 0)} {text}\n"

    elif block_type in ("paragraph", "text") and text:
        return f"{text}\n"

    elif block_type == "table" and block.get("rows"):
        rows = block["rows"]
        if not rows:
            return ""

        def get_cell_text(cell: dict) -> str:
            """Extract text from cell spans."""
            spans = cell.get("spans", [])
            if spans:
                return (
                    " ".join(s.get("text", "") for s in spans)
                    .strip()
                    .replace("|", "\\|")
                )
            return cell.get("text", "").strip().replace("|", "\\|")

        md_lines: list[str] = []
        header_cells = rows[0].get("cells", [])
        header = [get_cell_text(c) for c in header_cells]
        if any(header):
            md_lines.append("| " + " | ".join(header) + " |")
            md_lines.append("| " + " | ".join("---" for _ in header) + " |")

        for row in rows[1:]:
            row_cells = row.get("cells", [])
            row_text = [get_cell_text(c) for c in row_cells]
            md_lines.append("| " + " | ".join(row_text) + " |")

        md_lines.append("")
        return "\n".join(md_lines)

    elif block_type == "list":
        items = block.get("items", [])
        if items:
            md_lines = []
            for item in items:
                item_spans = item.get("spans", [])
                item_text = _join_styled_spans(item_spans)
                if not item_text:
                    continue

                indent = item.get("indent", 0)
                prefix = item.get("prefix")

                if prefix:
                    marker = f"{prefix} "
                else:
                    marker = "- "

                md_lines.append(f"{'  ' * indent}{marker}{item_text.strip()}")
            return "\n".join(md_lines) + "\n"

        if text:
            lines = text.split("\n")
            return (
                "\n".join(f"- {line.strip()}" for line in lines if line.strip()) + "\n"
            )
        return ""

    elif block_type == "figure":
        return f"![Figure]({block.get('text', 'figure')})\n"

    return ""
