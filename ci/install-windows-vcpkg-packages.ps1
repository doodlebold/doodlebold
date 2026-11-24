# Installs GoogleTest and libjpeg-turbo through the runner's vcpkg, and points the windows-msvc presets at it through VCPKG_ROOT.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

vcpkg install gtest libjpeg-turbo --triplet x64-windows
"VCPKG_ROOT=$env:VCPKG_INSTALLATION_ROOT" >> $env:GITHUB_ENV
