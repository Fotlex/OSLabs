#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"

echo "[INFO] Project directory: $SCRIPT_DIR"
echo "[INFO] Build type: $BUILD_TYPE"

echo "[INFO] Updating source code from Git repository..."
git pull


echo "[INFO] Configuring project with CMake..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" ..

JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
echo "[INFO] Building project (jobs: $JOBS)..."
cmake --build . -- -j"$JOBS"

echo "[OK] Build completed: $SCRIPT_DIR/$BUILD_DIR/counter_app"