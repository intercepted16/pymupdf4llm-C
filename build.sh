#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR=${BUILD_DIR:-build/native}
CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release}

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE"
cmake --build "$BUILD_DIR" --target tomd --config "$CMAKE_BUILD_TYPE"
