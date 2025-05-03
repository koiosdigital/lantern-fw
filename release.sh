#!/bin/bash
BINARIES="0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/ota_data_initial.bin 0x20000 build/LANTERN-fw.bin"

zip_name="factory.zip"
json_file="flash_files.json"

# Start JSON array
echo "[" > "$json_file"

# Process pairs and build JSON
first=1
while read -r offset file; do
    base_file=$(basename "$file")
    zip -j "$zip_name" "$file"  # add file to zip, stripping folders
    if [ $first -eq 1 ]; then
        first=0
    else
        echo "," >> "$json_file"
    fi
    printf "  {\"offset\": \"%s\", \"file\": \"%s\"}" "$offset" "$base_file" >> "$json_file"
done < <(echo "$BINARIES" | xargs -n 2)

# Close JSON array
echo -e "\n]" >> "$json_file"

# Add JSON to zip
zip -j "$zip_name" "$json_file"

# Optionally, remove the standalone JSON file
rm "$json_file"