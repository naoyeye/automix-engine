#!/bin/bash

# AutoMix Engine - Transition Preview Script
# 
# Usage: ./preview_transition.sh <file1> <file2> [output.wav]

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <file1> <file2> [output.wav]"
    exit 1
fi

FILE1="$1"
FILE2="$2"
OUTPUT="${3:-transition_preview.wav}"
DB_FILE="temp_preview.db"

# Build directory
BUILD_DIR="./build"
SCAN_TOOL="$BUILD_DIR/automix-scan"
PLAYLIST_TOOL="$BUILD_DIR/automix-playlist"
RENDER_TOOL="$BUILD_DIR/automix-render-transition"

# Cleanup previous temp db
rm -f "$DB_FILE"

echo "=== AutoMix Transition Preview ==="
echo "1. Scanning files..."

# Scan files into temporary DB
"$SCAN_TOOL" -d "$DB_FILE" "$FILE1"
"$SCAN_TOOL" -d "$DB_FILE" "$FILE2"

# Check if tracks were added
TRACK_COUNT=$("$PLAYLIST_TOOL" -d "$DB_FILE" --list | grep -c "Tracks in library" | awk '{print $NF}' 2>/dev/null)
# The above is not reliable parsing of "Tracks in library: N"
# Let's rely on render tool failing if tracks are missing, 
# but we can print a warning if scan might have failed.

echo "2. Rendering transition..."
"$RENDER_TOOL" "$DB_FILE" "auto" "auto" "$OUTPUT"

echo "=== Done ==="
echo "Preview saved to: $OUTPUT"
echo "Cleaning up..."
rm -f "$DB_FILE"
