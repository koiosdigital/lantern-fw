#!/bin/bash

input_file="./build/flasher_args.json"
dest_dir="./output"

# Create destination directory if needed
mkdir -p "$dest_dir"

# Temporary file for JSON entries
temp_json=$(mktemp)

# Process flash files with offsets
jq -r --arg dest "$dest_dir" '.flash_files | to_entries[] | "\(.key)\t\(.value)"' "$input_file" | while IFS=$'\t' read -r offset filepath; do
    if [ -f "./build/$filepath" ]; then
        filename=$(basename "$filepath")
        new_path="$filename"
        cp "./build/$filepath" "./output/$new_path"
        # Append JSON object with offset and new path
        jq -n --arg offset "$offset" --arg file "$new_path" '{offset: $offset, file: $file}' >> "$temp_json"
    else
        echo "Warning: File not found - $filepath" >&2
    fi
done

# Create final JSON array
jq -s '.' "$temp_json" > $dest_dir/flash_files.json
rm "$temp_json"

#zip output
zip -j -r "$dest_dir/factory.zip" "$dest_dir"

echo "Operation completed. Offsets preserved in flash_files.json"
