cd ~/.local/share/Trash

for info in info/*.trashinfo; do
    if grep -q "pymupdf4llm-C-tmp-tmp" "$info"; then
        orig=$(grep ^Path= "$info" | cut -d= -f2-)
        base=$(basename "$orig")
        dir=$(dirname "$orig")
        mkdir -p "$dir"
        cp -r "files/$base" "$dir/"
    fi
done
