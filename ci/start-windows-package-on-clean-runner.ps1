# Unpacks the Windows zip, starts doodlebold.exe from it with a file, and checks that it quits cleanly, on a runner that never built
# it. The runner has no graphics card, so Mesa's software OpenGL stands in. Needs GH_TOKEN, to download Mesa.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
Set-Location (Join-Path $PSScriptRoot '..')

$mesaRelease = '26.2.5'  # a release of pal1000/mesa-dist-win, pinned so the check doesn't change under us
$env:QT_OPENGL = 'software'

Expand-Archive (Get-ChildItem build-windows-msvc-release/Doodlebold-*.zip).FullName -DestinationPath smoke
$bin = (Get-ChildItem smoke -Recurse -Filter doodlebold.exe).DirectoryName
gh release download $mesaRelease --repo pal1000/mesa-dist-win --pattern 'mesa3d-*-release-msvc.7z' --dir mesa
7z x (Get-ChildItem mesa/*.7z).FullName -omesa x64 -y | Out-Null
Copy-Item mesa/x64/opengl32.dll (Join-Path $bin 'opengl32sw.dll')
Copy-Item mesa/x64/*.dll $bin -Exclude opengl32.dll
tools/windows-start-and-quit.ps1 -Exe (Join-Path $bin 'doodlebold.exe') -Open tests/fixtures/imagemagick-8bit.psd -Seconds 20
exit $LASTEXITCODE
