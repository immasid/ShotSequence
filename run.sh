#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"

if ! command -v cmake >/dev/null 2>&1; then
    echo "Error: CMake is required." >&2
    exit 1
fi

JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')}"

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

if ! command -v exiftool >/dev/null 2>&1; then
    echo "Warning: ExifTool is not installed." >&2
fi

exec "$BUILD_DIR/ShotSequence" "$@"
