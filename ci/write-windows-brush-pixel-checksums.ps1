# Compares Windows' brush-pixel renders with Linux's dumps within the platform tolerance, then, when they pass, writes those cases'
# checksums into tests/fixtures/brush-pixels/checksums-windows-msvc.txt.
# Usage: ci/write-windows-brush-pixel-checksums.ps1 -Only <part of a case name> -LinuxDumps <folder from dump-linux-brush-pixels.sh>
param(
    [Parameter(Mandatory)][string]$Only,
    [Parameter(Mandatory)][string]$LinuxDumps
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
Set-Location (Join-Path $PSScriptRoot '..')

$brushPixels = 'build-windows-msvc/tests/doodlebold_brush_pixels.exe'
cmake --preset windows-msvc
cmake --build --preset windows-msvc --target doodlebold_brush_pixels
& $brushPixels --compare $LinuxDumps --within --only $Only
if ($LASTEXITCODE) { exit $LASTEXITCODE }  # write no checksums for cases that differ
& $brushPixels --write tests/fixtures/brush-pixels --checksums checksums-windows-msvc.txt --only $Only
