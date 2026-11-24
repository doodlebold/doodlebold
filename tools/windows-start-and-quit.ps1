# Starts doodlebold.exe with a file, lets it run, closes its window and checks that it quits cleanly.
# Usage: tools/windows-start-and-quit.ps1 -Exe path\to\doodlebold.exe [-Open file] [-Seconds 20] [-Screenshot out.png] [-ExpectExit 0]
# Exits non-zero when the program fails to start, crashes, hangs on closing or quits with another exit code than expected.
param(
    [Parameter(Mandatory)][string]$Exe,
    [string]$Open,
    [int]$Seconds = 20,
    [string]$Screenshot,
    [int]$ExpectExit = 0
)

$env:QT_FORCE_STDERR_LOGGING = '1'
$log = Join-Path ([IO.Path]::GetTempPath()) "doodlebold-smoke-$PID.txt"
$arguments = @()
if ($Open) { $arguments += "`"$((Resolve-Path $Open).Path)`"" }
$start = @{ FilePath = $Exe; PassThru = $true; RedirectStandardError = $log }
if ($arguments) { $start.ArgumentList = $arguments }
$p = Start-Process @start
$null = $p.Handle  # keeps the exit code readable after the process ends

function Show-Log { if (Test-Path $log) { Get-Content $log } }

Start-Sleep -Seconds $Seconds
if ($p.HasExited) {
    Write-Host ("doodlebold quit by itself with exit code 0x{0:X8}" -f $p.ExitCode)
    Show-Log
    exit 1
}
$p.Refresh()
Write-Host "window: '$($p.MainWindowTitle)'"

if ($Screenshot) {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bitmap.Save($Screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
}

if (-not $p.CloseMainWindow()) {
    Write-Host 'doodlebold has no main window to close'
    Stop-Process -Id $p.Id -Force
    Show-Log
    exit 1
}
if (-not $p.WaitForExit(60000)) {
    Write-Host 'doodlebold did not quit within 60 s of closing its window'
    Stop-Process -Id $p.Id -Force
    Show-Log
    exit 1
}
Show-Log
Write-Host ("exit code 0x{0:X8}, expected {1}" -f $p.ExitCode, $ExpectExit)
exit [int]($p.ExitCode -ne $ExpectExit)
