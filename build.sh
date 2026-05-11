#!/bin/bash
set -e

cd "$(dirname "$0")"

# Read version
BUILD_NUM=$(cat version.txt)
VER=$(printf "v0.%02d" "$BUILD_NUM")

echo "=== Building shared_km $VER ==="

# Build
cmake --build build-fresh --config Release --target shared_km 2>&1 | tail -3
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

# Copy with versioned name
cp build-fresh/apps/Release/shared_km.exe "shared_km/shared_km_${VER}.exe"
echo "=== Deployed: shared_km_${VER}.exe ==="

# Increment version
echo $((BUILD_NUM + 1)) > version.txt
NEXT=$(printf "v0.%02d" $((BUILD_NUM + 1)))
echo "=== Next version will be $NEXT ==="
