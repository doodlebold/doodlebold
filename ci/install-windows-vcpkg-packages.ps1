# Installs GoogleTest and libjpeg-turbo through the runner's vcpkg, and points the windows-msvc presets at it through VCPKG_ROOT.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

# By its path: in the Visual Studio environment, `vcpkg` on the PATH is Visual Studio's own.
& "$env:VCPKG_INSTALLATION_ROOT\vcpkg.exe" install gtest libjpeg-turbo --triplet x64-windows
"VCPKG_ROOT=$env:VCPKG_INSTALLATION_ROOT" >> $env:GITHUB_ENV
