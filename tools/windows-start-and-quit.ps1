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

Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class ProcessWindows {
    delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    const uint WM_CLOSE = 0x0010;
    // Closes the process's visible windows other than `main`, and gives their titles.
    public static List<string> CloseOthers(int processId, IntPtr main) {
        var closed = new List<string>();
        EnumWindows((window, data) => {
            uint pid;
            GetWindowThreadProcessId(window, out pid);
            if (pid == processId && window != main && IsWindowVisible(window)) {
                var title = new StringBuilder(256);
                GetWindowText(window, title, title.Capacity);
                closed.Add(title.ToString());
                PostMessage(window, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
            }
            return true;
        }, IntPtr.Zero);
        return closed;
    }
}
'@

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

# A dialog the file brings up, such as the notes of an import, disables the main window, which then can't be closed.
foreach ($title in [ProcessWindows]::CloseOthers($p.Id, $p.MainWindowHandle)) { Write-Host "closed dialog: '$title'" }
Start-Sleep -Seconds 1
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
