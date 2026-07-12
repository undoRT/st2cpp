#!/usr/bin/env bash
# build.sh — Build st2cpp on Linux
set -e

BUILD_DIR="build"
INSTALL_PREFIX="${1:-/usr/local}"

echo "=== st2cpp Build Script ==="
echo "Build dir : $BUILD_DIR"
echo "Install   : $INSTALL_PREFIX"
echo

# Check dependencies
for cmd in cmake g++; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: '$cmd' not found. Install it with:"
        echo "  sudo apt install build-essential cmake"
        exit 1
    fi
done

CMAKE_VER=$(cmake --version | head -1 | grep -oP '\d+\.\d+')
echo "CMake $CMAKE_VER / $(g++ --version | head -1)"
echo

# Configure
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"

# Build
cmake --build "$BUILD_DIR" --parallel "$(nproc)"