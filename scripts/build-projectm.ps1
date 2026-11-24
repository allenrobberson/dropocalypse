# Builds projectM (the MilkDrop engine, LGPL-2.1) as projectM-4.dll for the Windows app.
# Dropocalypse loads it at run time, so the app itself builds without it.
#
#   ./scripts/build-projectm.ps1 [-Prefix build/projectm]
param([string]$Prefix = "build/projectm")
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot "..")
# A fixed commit of projectM's main branch (4.2 development, 2026-11-20): it can draw into our
# own framebuffer (projectm_opengl_render_frame_fbo), which the 4.1 releases can't.
$commit = "9e26f5b5bf89b015f197a16a61bcb21f4946d985"
$src = "build/projectm-src"
$have = ""
if (Test-Path "$src/.git") { $have = (git -C $src rev-parse HEAD 2>$null) }
if ($have -ne $commit) {
    if (Test-Path $src) { Remove-Item -Recurse -Force $src }
    git init --quiet $src
    git -C $src remote add origin https://github.com/projectM-visualizer/projectm.git
    git -C $src fetch --quiet --depth 1 origin $commit
    if ($LASTEXITCODE -ne 0) { throw "fetch failed" }
    git -C $src checkout --quiet FETCH_HEAD
    git -C $src submodule update --quiet --init --depth 1
    if ($LASTEXITCODE -ne 0) { throw "submodules failed" }
}
New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
$prefixFull = (Resolve-Path $Prefix).Path
# Static C runtime, like Dropocalypse.exe, so no Visual C++ Redistributable is needed.
cmake -S $src -B build/projectm-build -A x64 -DBUILD_SHARED_LIBS=ON -DENABLE_PLAYLIST=OFF -DENABLE_SDL_UI=OFF `
    -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF -DENABLE_SYSTEM_GLM=OFF -DENABLE_SYSTEM_PROJECTM_EVAL=OFF `
    -DENABLE_DEBUG_POSTFIX=OFF "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded" -DCMAKE_POLICY_DEFAULT_CMP0091=NEW `
    "-DCMAKE_INSTALL_PREFIX=$prefixFull"
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
cmake --build build/projectm-build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }
cmake --install build/projectm-build --config Release
if ($LASTEXITCODE -ne 0) { throw "install failed" }
Get-ChildItem "$prefixFull/bin"
