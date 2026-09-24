#!/usr/bin/env bash
# Build the st2cpp CLI and transpile this example with external libraries.
#
#   ./build.sh
#
# The external libraries are NOT passed one by one: st2cpp receives only the
# project JSON (project.json) via --ext-libs, loads every library descriptor
# declared there, and resolves the symbols used by main.st during transpilation.
set -euo pipefail
cd "$(dirname "$0")"

REPO="$(cd ../.. && pwd)"
BUILD="${REPO}/build"

echo "== building st2cpp CLI (out-of-source) =="
cmake -S "$REPO" -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target st2cpp -j "$(nproc)" >/dev/null

echo
echo "== transpiling the workspace with --ext-libs project.json =="
rm -rf generated
"$BUILD/st2cpp" --workspace . --output-dir generated --ext-libs project.json --strict

echo
echo "== syntax-check of the generated C++ (mock/ headers + undoCore runtime) =="
g++ -std=c++17 -fsyntax-only generated/main.cpp \
    -I generated -I mock \
    -I "$REPO/st2cpp_includes/undoCore/include"
echo "OK: generated code compiles against mock/ and the undoCore runtime"