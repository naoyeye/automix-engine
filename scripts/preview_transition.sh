#!/bin/bash

# AutoMix Engine - Transition Preview Script
#
# Renders a transition between two audio files without using the main library.
# Creates a temporary database, scans both files, renders the transition, then cleans up.
#
# Usage: ./scripts/preview_transition.sh <file1> <file2> [output.wav]

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <file1> <file2> [output.wav]"
    exit 1
fi

FILE1="$1"
FILE2="$2"
OUTPUT="${3:-transition_preview.wav}"

# Resolve script location for correct paths when run from any directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/cmake-build"
DB_FILE="$PROJECT_ROOT/temp_preview.db"

SCAN_TOOL="$BUILD_DIR/automix-scan"
PLAYLIST_TOOL="$BUILD_DIR/automix-playlist"
RENDER_TOOL="$BUILD_DIR/automix-render-transition"

# Cleanup previous temp db and SQLite WAL files
rm -f "$DB_FILE" "${DB_FILE}-wal" "${DB_FILE}-shm"

echo "=== AutoMix Transition Preview ==="
echo "1. Scanning files..."

# Scan files into temporary DB
"$SCAN_TOOL" -d "$DB_FILE" "$FILE1"
"$SCAN_TOOL" -d "$DB_FILE" "$FILE2"

echo "2. Rendering transition..."
"$RENDER_TOOL" "$DB_FILE" "auto" "auto" "$OUTPUT"

echo "=== Done ==="
echo "Preview saved to: $OUTPUT"
echo "Cleaning up..."
rm -f "$DB_FILE" "${DB_FILE}-wal" "${DB_FILE}-shm"
