# Packages the unsigned Microsoft Store MSIX. The Store signs the package itself.
# Identity defaults match Partner Center for Store ID 9N1TJ9D5R5C8.
# Repository variables STORE_IDENTITY_NAME, STORE_PUBLISHER and
# STORE_PUBLISHER_DISPLAY_NAME override the defaults when set.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$ExePath,
    [string]$Stage = "msix-stage",
    [string]$OutputDir = "store-dist"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Store build not found at $ExePath"
}

$parts = (($Version -split '[-+]')[0]).Split('.')
if ($parts.Count -lt 3 -or $parts[0] -eq "" -or $parts[1] -eq "" -or $parts[2] -eq "") {
    throw "Store version must look like 1.2.6 (got '$Version')."
}
$msixVersion = "$($parts[0]).$($parts[1]).$($parts[2]).0"

$name = if ($env:STORE_IDENTITY_NAME) { $env:STORE_IDENTITY_NAME } else { "AllenRobberson.Dropocalypse" }
$pub  = if ($env:STORE_PUBLISHER) { $env:STORE_PUBLISHER } else { "CN=E2C97CD9-87D6-4B67-95B2-E279CF095C7A" }
$disp = if ($env:STORE_PUBLISHER_DISPLAY_NAME) { $env:STORE_PUBLISHER_DISPLAY_NAME } else { "Allen Robberson" }

New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item -LiteralPath $ExePath -Destination (Join-Path $Stage "Dropocalypse.exe")
# The MilkDrop engine (projectM-4.dll and glew32.dll) sits beside the exe.
Get-ChildItem -LiteralPath (Split-Path -Parent $ExePath) -Filter *.dll | Copy-Item -Destination $Stage
Copy-Item -Recurse -Force windows/store/Assets (Join-Path $Stage "Assets")
Copy-Item LICENSE (Join-Path $Stage "LICENSE.txt")

$manifest = Get-Content windows/store/AppxManifest.xml -Raw
$manifest = $manifest.Replace('__IDENTITY_NAME__', $name).Replace('__PUBLISHER__', $pub)
$manifest = $manifest.Replace('__PUBLISHER_DISPLAY_NAME__', [System.Security.SecurityElement]::Escape($disp))
$manifest = $manifest.Replace('__VERSION__', $msixVersion)
Set-Content -Encoding utf8 -Path (Join-Path $Stage "AppxManifest.xml") -Value $manifest

$makeappx = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter makeappx.exe |
    Where-Object { $_.FullName -match '\\x64\\' } |
    Sort-Object FullName |
    Select-Object -Last 1
if (-not $makeappx) { throw "makeappx.exe not found" }

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$safeVersion = ($Version -split '[-+]')[0]
$out = Join-Path $OutputDir "Dropocalypse-$safeVersion-Store.msix"
& $makeappx.FullName pack /d $Stage /p $out /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed" }

Write-Host "Packed $out (package version $msixVersion)"
Get-ChildItem $OutputDir
