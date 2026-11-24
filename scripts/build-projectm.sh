#!/usr/bin/env bash
# Builds projectM (the MilkDrop engine, LGPL-2.1) as a shared library for the Mac and Linux apps.
# Dropocalypse loads it at run time, so the app itself builds without it.
#
#   scripts/build-projectm.sh [install-dir]     # default: build/projectm
#
# macOS builds a universal (arm64 + x86_64) dylib for macOS 13 and later.
set -euo pipefail
cd "$(dirname "$0")/.."
PREFIX="${1:-build/projectm}"
PREFIX="$(mkdir -p "$PREFIX" && cd "$PREFIX" && pwd)"
TAG="v4.1.8"
SRC="build/projectm-src"

if [ ! -d "$SRC/.git" ]; then
  rm -rf "$SRC"
  git clone --quiet --depth 1 --branch "$TAG" --recurse-submodules --shallow-submodules \
    https://github.com/projectM-visualizer/projectm.git "$SRC"
fi

EXTRA=()
if [ "$(uname)" = "Darwin" ]; then
  EXTRA=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
         -DCMAKE_INSTALL_NAME_DIR=@rpath)
fi
cmake -S "$SRC" -B build/projectm-build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
  -DENABLE_PLAYLIST=OFF -DENABLE_SDL_UI=OFF -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF \
  -DENABLE_SYSTEM_GLM=OFF -DENABLE_SYSTEM_PROJECTM_EVAL=OFF \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" ${EXTRA[@]+"${EXTRA[@]}"}
cmake --build build/projectm-build --config Release --parallel
cmake --install build/projectm-build --config Release
echo "projectM installed in $PREFIX"
ls -la "$PREFIX/lib"
