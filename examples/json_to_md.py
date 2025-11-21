import json
import sys

def json_to_md(input_file, output_file):
    with open(input_file, 'r', encoding='utf-8') as f:
        data = json.load(f)

    md_lines = []

    for item in data:
        item_type = item.get("type")
        text = item.get("text", "").strip()

        if item_type == "heading" and text:
            # Use font size to determine heading level (optional)
            level = 1 if item.get("font_size", 0) > 20 else 2
            md_lines.append(f"{'#' * level} {text}\n")

        elif item_type == "paragraph" and text:
            md_lines.append(f"{text}\n")

        elif item_type == "table" and item.get("rows"):
            rows = item["rows"]
            if not rows:
                continue

            # Build header
            header_cells = rows[0].get("cells", [])
            header = [c.get("text", "").strip() for c in header_cells]
            if any(header):
                md_lines.append("| " + " | ".join(header) + " |")
                md_lines.append("| " + " | ".join("---" for _ in header) + " |")

            # Add remaining rows
            for row in rows[1:]:
                row_cells = row.get("cells", [])
                row_text = [c.get("text", "").strip() for c in row_cells]
                md_lines.append("| " + " | ".join(row_text) + " |")
            md_lines.append("")  # empty line after table

    # Write to file
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("\n".join(md_lines))

    print(f"Markdown saved to {output_file}")

# Example usage
# json_to_md("extracted.json", "output.md")

if __name__ == "__main__":
    json_to_md(sys.argv[1], sys.argv[2])
