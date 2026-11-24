# Builds projectM (the MilkDrop engine, LGPL-2.1) as projectM-4.dll for the Windows app, with the
# glew32.dll it needs. Dropocalypse loads it at run time, so the app itself builds without it.
#
#   ./scripts/build-projectm.ps1 [-Prefix build/projectm]
param([string]$Prefix = "build/projectm")
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot "..")
$tag = "v4.1.8"
$src = "build/projectm-src"
if (-not (Test-Path "$src/.git")) {
    if (Test-Path $src) { Remove-Item -Recurse -Force $src }
    git clone --quiet --depth 1 --branch $tag --recurse-submodules --shallow-submodules `
        https://github.com/projectM-visualizer/projectm.git $src
    if ($LASTEXITCODE -ne 0) { throw "clone failed" }
}
$vcpkg = $env:VCPKG_INSTALLATION_ROOT
if (-not $vcpkg) { throw "VCPKG_INSTALLATION_ROOT is not set" }
New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
$prefixFull = (Resolve-Path $Prefix).Path
# GLEW comes from vcpkg (projectM's manifest); GLM and the expression library from its submodules.
cmake -S $src -B build/projectm-build -A x64 "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
    -DBUILD_SHARED_LIBS=ON -DENABLE_PLAYLIST=OFF -DENABLE_SDL_UI=OFF -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF `
    -DENABLE_SYSTEM_GLM=OFF -DENABLE_SYSTEM_PROJECTM_EVAL=OFF "-DCMAKE_INSTALL_PREFIX=$prefixFull"
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
cmake --build build/projectm-build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }
cmake --install build/projectm-build --config Release
if ($LASTEXITCODE -ne 0) { throw "install failed" }
$glew = Get-ChildItem build/projectm-build -Recurse -Filter "glew32.dll" | Select-Object -First 1
if (-not $glew) { throw "glew32.dll not found" }
Copy-Item $glew.FullName "$prefixFull/bin/" -Force
Get-ChildItem "$prefixFull/bin"
