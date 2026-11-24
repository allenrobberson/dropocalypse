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
# A fixed commit of projectM's main branch (4.2 development, 2026-11-20): it can draw into our
# own framebuffer (projectm_opengl_render_frame_fbo), which the 4.1 releases can't.
COMMIT="9e26f5b5bf89b015f197a16a61bcb21f4946d985"
SRC="build/projectm-src"

if [ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null)" != "$COMMIT" ]; then
  rm -rf "$SRC"
  git init --quiet "$SRC"
  git -C "$SRC" remote add origin https://github.com/projectM-visualizer/projectm.git
  git -C "$SRC" fetch --quiet --depth 1 origin "$COMMIT"
  git -C "$SRC" checkout --quiet FETCH_HEAD
  git -C "$SRC" submodule update --quiet --init --depth 1
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
