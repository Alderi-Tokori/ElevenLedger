param(
    [string]$ExePath = "$PSScriptRoot\..\build\pitchrate.exe",
    [string]$SeedPath = "$PSScriptRoot\..\build\pitchrate_demo_seed.exe"
)

$ErrorActionPreference = "Stop"
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public class PitchRateSmoke {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    public static string ClassName(IntPtr hWnd) {
        var value = new StringBuilder(128);
        GetClassName(hWnd, value, value.Capacity);
        return value.ToString();
    }

    public static string WindowText(IntPtr hWnd) {
        var value = new StringBuilder(512);
        GetWindowText(hWnd, value, value.Capacity);
        return value.ToString();
    }

    public static IntPtr FindTopLevel(uint processId, string className) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            uint owner;
            GetWindowThreadProcessId(hWnd, out owner);
            if (owner == processId && ClassName(hWnd) == className) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindChildByClass(IntPtr parent, string className, bool visibleOnly) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (hWnd, lParam) => {
            if (ClassName(hWnd) == className && (!visibleOnly || IsWindowVisible(hWnd))) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindChildById(IntPtr parent, int id) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (hWnd, lParam) => {
            if (GetDlgCtrlID(hWnd) == id) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

[PitchRateSmoke]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null

function Wait-Until {
    param(
        [scriptblock]$Condition,
        [int]$TimeoutMilliseconds = 10000,
        [string]$Description = "condition"
    )
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((& $Condition) -eq $true) {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for $Description."
}

$exe = (Resolve-Path $ExePath).Path
$seed = (Resolve-Path $SeedPath).Path
$tempDirectory = Join-Path $env:TEMP ("PitchRateSmoke-" + [Guid]::NewGuid().ToString("N"))
$dataPath = Join-Path $tempDirectory "smoke.frc"
New-Item -ItemType Directory -Path $tempDirectory | Out-Null
$process = $null

try {
    & $seed $dataPath | Out-Null
    if (-not (Test-Path $dataPath)) {
        throw "The demo seed did not create a data file."
    }

    $process = Start-Process -FilePath $exe -ArgumentList @("--data", $dataPath) -PassThru
    Wait-Until -Description "the main window" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMainWindow")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            ([PitchRateSmoke]::WindowText($candidate) -like "PitchRate*"))
    }
    $main = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMainWindow")
    $title = [PitchRateSmoke]::WindowText($main)
    Write-Host "Main window OK: $title"

    $pageCommands = @(40001, 40002, 40003, 40004, 40005)
    $pageNames = @("Dashboard", "Players", "Teams", "Matches", "Rating guide")
    for ($i = 0; $i -lt $pageCommands.Count; $i++) {
        [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]$pageCommands[$i], [IntPtr]::Zero) | Out-Null
        $expectedTitle = $pageNames[$i]
        Wait-Until -Description "the $expectedTitle page" -Condition {
            $candidate = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
            return ($candidate -ne [IntPtr]::Zero -and
                [PitchRateSmoke]::WindowText($candidate) -eq $expectedTitle)
        }
        $visiblePage = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
        if ($visiblePage -eq [IntPtr]::Zero) {
            throw "No visible page after navigating to $expectedTitle."
        }
        Write-Host "Page OK: $expectedTitle"
    }

    [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]40003, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 250
    $teamsPage = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
    [PitchRateSmoke]::PostMessage($teamsPage, 0x0111, [IntPtr]1101, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the team dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateTeamDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($main))
    }
    $teamDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateTeamDialog")
    if ([PitchRateSmoke]::IsWindowEnabled($main)) {
        throw "The owner window stayed enabled while a modal dialog was open."
    }
    if ([PitchRateSmoke]::FindChildById($teamDialog, 2003) -eq [IntPtr]::Zero) {
        throw "The team dialog is missing its save button."
    }
    [PitchRateSmoke]::PostMessage($teamDialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the team dialog to close" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateTeamDialog") -eq [IntPtr]::Zero)
    }
    if (-not [PitchRateSmoke]::IsWindowEnabled($main)) {
        throw "The owner window was not re-enabled after closing the dialog."
    }
    Write-Host "Modal team dialog OK"

    [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]40004, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 250
    $matchesPage = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
    $matchesList = [PitchRateSmoke]::FindChildById($matchesPage, 1300)
    if ($matchesList -eq [IntPtr]::Zero) {
        throw "The matches page is missing its match list."
    }
    $matchesBefore = [PitchRateSmoke]::SendMessage($matchesList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    [PitchRateSmoke]::PostMessage($matchesPage, 0x0111, [IntPtr]1301, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the record-match dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($main))
    }
    $recordDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
    $lineupList = [PitchRateSmoke]::FindChildById($recordDialog, 2205)
    if ($lineupList -eq [IntPtr]::Zero) {
        throw "The record-match dialog is missing its line-up list."
    }
    $lineupCount = [PitchRateSmoke]::SendMessage($lineupList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($lineupCount -lt 2) {
        throw "The record-match dialog did not load any players."
    }
    $neutralCheckbox = [PitchRateSmoke]::FindChildById($recordDialog, 2210)
    if ($neutralCheckbox -eq [IntPtr]::Zero) {
        throw "The record-match dialog is missing its neutral-venue control."
    }
    [PitchRateSmoke]::PostMessage($neutralCheckbox, 0x0201, [IntPtr]1, [IntPtr]10) | Out-Null
    [PitchRateSmoke]::PostMessage($neutralCheckbox, 0x0202, [IntPtr]0, [IntPtr]10) | Out-Null
    [PitchRateSmoke]::PostMessage($recordDialog, 0x0111, [IntPtr]2206, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 100
    [PitchRateSmoke]::PostMessage($recordDialog, 0x0111, [IntPtr]2208, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the match-details dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDetailsDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate))
    }
    $detailsDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDetailsDialog")
    [PitchRateSmoke]::PostMessage($detailsDialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the details dialog to close" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDetailsDialog") -eq [IntPtr]::Zero)
    }
    Wait-Until -Description "the owner to be re-enabled" -Condition {
        return [PitchRateSmoke]::IsWindowEnabled($main)
    }
    $matchesPage = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
    $matchesList = [PitchRateSmoke]::FindChildById($matchesPage, 1300)
    $matchesAfter = [PitchRateSmoke]::SendMessage($matchesList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($matchesAfter -ne $matchesBefore + 1) {
        throw "The saved match was not reflected in the match list."
    }
    Write-Host "Record and save flow OK"

    [PitchRateSmoke]::PostMessage($main, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the application to exit" -Condition {
        $process.Refresh()
        return $process.HasExited
    } -TimeoutMilliseconds 15000
    if ($process.ExitCode -ne 0) {
        throw "PitchRate exited with code $($process.ExitCode)."
    }
    Write-Host "GUI smoke test passed."
}
finally {
    if ($process -ne $null) {
        $process.Refresh()
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
    if (Test-Path $tempDirectory) {
        Remove-Item -Recurse -Force $tempDirectory -ErrorAction SilentlyContinue
    }
}
