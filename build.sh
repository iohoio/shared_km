#!/bin/bash
set -e

cd "$(dirname "$0")"

VER=$(printf "v0.%02d" "$(cat version.txt)")
echo "=== Building shared_km $VER ==="

cmake --build build-fresh --config Release --target shared_km 2>&1 | tail -3
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "=== Built: build-fresh/apps/Release/shared_km_${VER}.exe ==="
echo "=== Next version: v0.$(printf "%02d" $(($(cat version.txt) + 1))) ==="
