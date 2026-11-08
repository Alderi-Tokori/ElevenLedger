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
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessageText(IntPtr hWnd, uint message, IntPtr wParam, string lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessageBuffer(IntPtr hWnd, uint message, IntPtr wParam, StringBuilder lParam);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hWnd, uint command);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint attach, uint attachTo, bool value);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hWnd);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

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

    public static string ControlText(IntPtr hWnd) {
        var value = new StringBuilder(512);
        SendMessageBuffer(hWnd, 0x000D, (IntPtr)value.Capacity, value);
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

    public static IntPtr FindChildButtonByText(IntPtr parent, string text) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (hWnd, lParam) => {
            if (ClassName(hWnd) == "Button" && WindowText(hWnd) == text) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindChildEditByText(IntPtr parent, string text) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (hWnd, lParam) => {
            if (ClassName(hWnd) == "Edit" && IsWindowVisible(hWnd) &&
                ControlText(hWnd) == text) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static void ForceForeground(IntPtr window) {
        IntPtr foreground = GetForegroundWindow();
        uint foregroundPid;
        uint foregroundThread = GetWindowThreadProcessId(foreground, out foregroundPid);
        uint currentThread = GetCurrentThreadId();
        if (foregroundThread != 0 && foregroundThread != currentThread) {
            AttachThreadInput(currentThread, foregroundThread, true);
        }
        ShowWindow(window, 9);
        BringWindowToTop(window);
        SetForegroundWindow(window);
        SetFocus(window);
        if (foregroundThread != 0 && foregroundThread != currentThread) {
            AttachThreadInput(currentThread, foregroundThread, false);
        }
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

function Assert-CellEditor {
    param(
        [IntPtr]$Dialog,
        [IntPtr]$List,
        [int]$SubItem,
        [string]$ExpectedText,
        [string]$Label
    )

    [PitchRateSmoke]::ForceForeground($Dialog)
    $listRect = New-Object PitchRateSmoke+RECT
    [PitchRateSmoke]::GetWindowRect($List, [ref]$listRect) | Out-Null
    $header = [PitchRateSmoke]::SendMessage($List, 0x101F, [IntPtr]::Zero, [IntPtr]::Zero)
    $headerRect = New-Object PitchRateSmoke+RECT
    [PitchRateSmoke]::GetWindowRect($header, [ref]$headerRect) | Out-Null

    $x = 2
    for ($i = 0; $i -lt $SubItem; $i++) {
        $x += [int][PitchRateSmoke]::SendMessage(
            $List, 0x101D, [IntPtr]$i, [IntPtr]::Zero).ToInt64()
    }
    $x += [int]([PitchRateSmoke]::SendMessage(
        $List, 0x101D, [IntPtr]$SubItem, [IntPtr]::Zero).ToInt64() / 2)
    $y = ($headerRect.Bottom - $listRect.Top) + 12

    $point = New-Object PitchRateSmoke+POINT
    $point.X = $x
    $point.Y = $y
    [PitchRateSmoke]::ClientToScreen($List, [ref]$point) | Out-Null
    [PitchRateSmoke]::SetCursorPos($point.X, $point.Y) | Out-Null
    [PitchRateSmoke]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [PitchRateSmoke]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [PitchRateSmoke]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [PitchRateSmoke]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)

    Wait-Until -Description "$Label cell editor" -Condition {
        return ([PitchRateSmoke]::FindChildEditByText($Dialog, $ExpectedText) -ne [IntPtr]::Zero)
    }
    $editor = [PitchRateSmoke]::FindChildEditByText($Dialog, $ExpectedText)
    [PitchRateSmoke]::SetCursorPos($point.X, $point.Y + 70) | Out-Null
    Start-Sleep -Milliseconds 100
    $above = [PitchRateSmoke]::GetWindow($editor, 3)
    if ($above -ne [IntPtr]::Zero) {
        throw "The $Label cell editor is not above the line-up list."
    }
    [PitchRateSmoke]::PostMessage($editor, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "$Label cell editor to close" -Condition {
        return ([PitchRateSmoke]::FindChildEditByText($Dialog, $ExpectedText) -eq [IntPtr]::Zero)
    }
    Write-Host "Cell editing OK: $Label"
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

    [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]40002, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the Players page for the player dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::WindowText($candidate) -eq "Players")
    }
    # CMD_CREATE_ENTRY is the command Ctrl+N sends; it must open the page's create dialog.
    [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]40000, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the player dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($main))
    }
    Write-Host "Create command OK: Players"
    $playerDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
    $defaultRatingEdit = [PitchRateSmoke]::FindChildById($playerDialog, 2103)
    if ($defaultRatingEdit -eq [IntPtr]::Zero) {
        throw "The player dialog is missing its starting-rating control."
    }
    $defaultRating = [PitchRateSmoke]::WindowText($defaultRatingEdit)
    if ($defaultRating -ne "50.00") {
        throw "Unexpected default starting rating: '$defaultRating'."
    }
    $playerTeamCombo = [PitchRateSmoke]::FindChildById($playerDialog, 2102)
    if ($playerTeamCombo -eq [IntPtr]::Zero) {
        throw "The player dialog is missing its team control."
    }
    $selectedTeamIndex = [PitchRateSmoke]::SendMessage(
        $playerTeamCombo, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($selectedTeamIndex -ne 1) {
        throw "The Add Player team did not default to the previous player's team."
    }
    Write-Host "Default starting rating OK: $defaultRating"
    Write-Host "Previous player's team default OK: $selectedTeamIndex"

    $playersPage = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
    $playerList = [PitchRateSmoke]::FindChildById($playersPage, 1200)
    if ($playerList -eq [IntPtr]::Zero) {
        throw "The players page is missing its player list."
    }
    $playersBefore = [PitchRateSmoke]::SendMessage(
        $playerList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()

    $playerFirst = [PitchRateSmoke]::FindChildById($playerDialog, 2100)
    $playerLast = [PitchRateSmoke]::FindChildById($playerDialog, 2101)
    $playerSave = [PitchRateSmoke]::FindChildById($playerDialog, 2105)
    if ($playerFirst -eq [IntPtr]::Zero -or $playerLast -eq [IntPtr]::Zero -or
        $playerSave -eq [IntPtr]::Zero) {
        throw "The player dialog is missing its name fields or save button."
    }
    # Oliver Hart is a seeded Northbridge player. The default club is Riverside,
    # so the duplicate warning must offer both adding a duplicate and moving him.
    [PitchRateSmoke]::SendMessageText($playerFirst, 0x000C, [IntPtr]::Zero, "Oliver") | Out-Null
    [PitchRateSmoke]::SendMessageText($playerLast, 0x000C, [IntPtr]::Zero, "Hart") | Out-Null
    [PitchRateSmoke]::PostMessage($playerDialog, 0x0111, [IntPtr]2105, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the duplicate-player warning" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "#32770")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            ([PitchRateSmoke]::WindowText($candidate) -like "Duplicate player*") -and
            -not [PitchRateSmoke]::IsWindowEnabled($playerDialog))
    }
    $duplicateDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "#32770")
    $addDuplicate = [PitchRateSmoke]::FindChildButtonByText(
        $duplicateDialog, "Add duplicate")
    $moveExisting = [PitchRateSmoke]::FindChildButtonByText(
        $duplicateDialog, "Move existing player")
    if ($addDuplicate -eq [IntPtr]::Zero -or $moveExisting -eq [IntPtr]::Zero) {
        throw "The duplicate-player warning is missing its choices."
    }
    [PitchRateSmoke]::SendMessage($addDuplicate, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the duplicate player to be saved" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "#32770") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($main))
    }
    $playersPage = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
    $playerList = [PitchRateSmoke]::FindChildById($playersPage, 1200)
    $playersAfter = [PitchRateSmoke]::SendMessage(
        $playerList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($playersAfter -ne $playersBefore + 1) {
        throw "The confirmed duplicate player was not added to the player list."
    }
    Write-Host "Duplicate player warning and add flow OK"

    [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]40000, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the second player dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($main))
    }
    $playerDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
    $playerFirst = [PitchRateSmoke]::FindChildById($playerDialog, 2100)
    $playerLast = [PitchRateSmoke]::FindChildById($playerDialog, 2101)
    [PitchRateSmoke]::SendMessageText($playerFirst, 0x000C, [IntPtr]::Zero, "Oliver") | Out-Null
    [PitchRateSmoke]::SendMessageText($playerLast, 0x000C, [IntPtr]::Zero, "Hart") | Out-Null
    [PitchRateSmoke]::PostMessage($playerDialog, 0x0111, [IntPtr]2105, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the second duplicate-player warning" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "#32770")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            ([PitchRateSmoke]::WindowText($candidate) -like "Duplicate player*") -and
            -not [PitchRateSmoke]::IsWindowEnabled($playerDialog))
    }
    $duplicateDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "#32770")
    $moveExisting = [PitchRateSmoke]::FindChildButtonByText(
        $duplicateDialog, "Move existing player")
    if ($moveExisting -eq [IntPtr]::Zero) {
        throw "The duplicate-player warning is missing its move choice."
    }
    [PitchRateSmoke]::SendMessage($moveExisting, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the existing player to be moved" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "#32770") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($main))
    }
    $playersPage = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
    $playerList = [PitchRateSmoke]::FindChildById($playersPage, 1200)
    $playersAfterMove = [PitchRateSmoke]::SendMessage(
        $playerList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($playersAfterMove -ne $playersAfter) {
        throw "Moving the existing player changed the player count."
    }
    Write-Host "Duplicate player move flow OK"

    [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]40003, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 250
    [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]40000, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the team dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateTeamDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($main))
    }
    Write-Host "Create command OK: Teams"
    $teamDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateTeamDialog")
    if ([PitchRateSmoke]::IsWindowEnabled($main)) {
        throw "The owner window stayed enabled while a modal dialog was open."
    }
    $teamName = [PitchRateSmoke]::FindChildById($teamDialog, 2000)
    $teamSave = [PitchRateSmoke]::FindChildById($teamDialog, 2003)
    if ($teamName -eq [IntPtr]::Zero -or $teamSave -eq [IntPtr]::Zero) {
        throw "The team dialog is missing its name field or save button."
    }
    [PitchRateSmoke]::SendMessageText($teamName, 0x000C, [IntPtr]::Zero, "Empty FC") | Out-Null
    [PitchRateSmoke]::PostMessage($teamDialog, 0x0111, [IntPtr]2003, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the empty team to be saved" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateTeamDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($main))
    }
    Write-Host "Empty team creation OK"

    [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]40004, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 250
    $matchesPage = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
    $matchesList = [PitchRateSmoke]::FindChildById($matchesPage, 1300)
    if ($matchesList -eq [IntPtr]::Zero) {
        throw "The matches page is missing its match list."
    }
    $matchesBefore = [PitchRateSmoke]::SendMessage($matchesList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    [PitchRateSmoke]::PostMessage($main, 0x0111, [IntPtr]40000, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the record-match dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($main))
    }
    Write-Host "Create command OK: Matches"
    $recordDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
    $homeLineupList = [PitchRateSmoke]::FindChildById($recordDialog, 2205)
    $awayLineupList = [PitchRateSmoke]::FindChildById($recordDialog, 2211)
    $homeAddPlayer = [PitchRateSmoke]::FindChildById($recordDialog, 2212)
    $awayAddPlayer = [PitchRateSmoke]::FindChildById($recordDialog, 2213)
    if ($homeLineupList -eq [IntPtr]::Zero -or
        $awayLineupList -eq [IntPtr]::Zero -or
        $homeAddPlayer -eq [IntPtr]::Zero -or
        $awayAddPlayer -eq [IntPtr]::Zero) {
        throw "The record-match dialog is missing its side-by-side line-up lists or add-player buttons."
    }
    $homeLineupCount = [PitchRateSmoke]::SendMessage(
        $homeLineupList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $awayLineupCount = [PitchRateSmoke]::SendMessage(
        $awayLineupList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($homeLineupCount -lt 1 -or $awayLineupCount -lt 1) {
        throw "The record-match dialog did not split both teams into their own lists."
    }
    Write-Host "Side-by-side line-up lists OK"

    Assert-CellEditor -Dialog $recordDialog -List $homeLineupList `
        -SubItem 1 -ExpectedText "90" -Label "home minutes"
    Assert-CellEditor -Dialog $recordDialog -List $homeLineupList `
        -SubItem 2 -ExpectedText "6.00" -Label "home performance"
    Assert-CellEditor -Dialog $recordDialog -List $awayLineupList `
        -SubItem 1 -ExpectedText "90" -Label "away minutes"
    Assert-CellEditor -Dialog $recordDialog -List $awayLineupList `
        -SubItem 2 -ExpectedText "6.00" -Label "away performance"

    $homeTeamCombo = [PitchRateSmoke]::FindChildById($recordDialog, 2201)
    $homeTeamIndex = [PitchRateSmoke]::SendMessage(
        $homeTeamCombo, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $homeTeamData = [PitchRateSmoke]::SendMessage(
        $homeTeamCombo, 0x0150, [IntPtr]$homeTeamIndex, [IntPtr]::Zero).ToInt64()

    $emptyTeamIndex = [PitchRateSmoke]::SendMessageText(
        $homeTeamCombo, 0x0158, [IntPtr](-1), "Empty FC").ToInt64()
    if ($emptyTeamIndex -lt 0) {
        throw "The club with no players is missing from the match team list."
    }
    $emptyTeamData = [PitchRateSmoke]::SendMessage(
        $homeTeamCombo, 0x0150, [IntPtr]$emptyTeamIndex, [IntPtr]::Zero).ToInt64()
    [PitchRateSmoke]::SendMessage(
        $homeTeamCombo, 0x014E, [IntPtr]$emptyTeamIndex, [IntPtr]::Zero) | Out-Null
    $selectionCommand = [IntPtr]((1 -shl 16) -bor 2201)
    [PitchRateSmoke]::PostMessage($recordDialog, 0x0111, $selectionCommand, $homeTeamCombo) | Out-Null
    Wait-Until -Description "the empty club line-up" -Condition {
        return ([PitchRateSmoke]::SendMessage(
            $homeLineupList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0)
    }

    [PitchRateSmoke]::PostMessage($recordDialog, 0x0111, [IntPtr]2212, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the add-player dialog for the empty club" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($recordDialog))
    }
    $emptyPlayerDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
    $emptyPlayerTeam = [PitchRateSmoke]::FindChildById($emptyPlayerDialog, 2102)
    if ($emptyPlayerTeam -eq [IntPtr]::Zero -or
        [PitchRateSmoke]::IsWindowEnabled($emptyPlayerTeam)) {
        throw "The empty-club add-player dialog did not lock its team."
    }
    $emptyPlayerTeamIndex = [PitchRateSmoke]::SendMessage(
        $emptyPlayerTeam, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $emptyPlayerTeamData = [PitchRateSmoke]::SendMessage(
        $emptyPlayerTeam, 0x0150, [IntPtr]$emptyPlayerTeamIndex, [IntPtr]::Zero).ToInt64()
    if ($emptyPlayerTeamData -ne $emptyTeamData) {
        throw "The empty-club add-player dialog opened for the wrong team."
    }
    $emptyPlayerFirst = [PitchRateSmoke]::FindChildById($emptyPlayerDialog, 2100)
    $emptyPlayerLast = [PitchRateSmoke]::FindChildById($emptyPlayerDialog, 2101)
    [PitchRateSmoke]::SendMessageText($emptyPlayerFirst, 0x000C, [IntPtr]::Zero, "Empty") | Out-Null
    [PitchRateSmoke]::SendMessageText($emptyPlayerLast, 0x000C, [IntPtr]::Zero, "Rookie") | Out-Null
    [PitchRateSmoke]::PostMessage($emptyPlayerDialog, 0x0111, [IntPtr]2105, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the empty-club player to be saved" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($recordDialog))
    }
    Wait-Until -Description "the empty club line-up to refresh" -Condition {
        return ([PitchRateSmoke]::SendMessage(
            $homeLineupList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1)
    }
    Write-Host "Empty club inline player OK"

    [PitchRateSmoke]::SendMessage(
        $homeTeamCombo, 0x014E, [IntPtr]$homeTeamIndex, [IntPtr]::Zero) | Out-Null
    [PitchRateSmoke]::PostMessage($recordDialog, 0x0111, $selectionCommand, $homeTeamCombo) | Out-Null
    Wait-Until -Description "the original home team line-up" -Condition {
        return ([PitchRateSmoke]::SendMessage(
            $homeLineupList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq $homeLineupCount)
    }
    [PitchRateSmoke]::PostMessage($recordDialog, 0x0111, [IntPtr]2212, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the add-player dialog from the match dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($recordDialog))
    }
    $addPlayerDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
    $addPlayerTeam = [PitchRateSmoke]::FindChildById($addPlayerDialog, 2102)
    if ($addPlayerTeam -eq [IntPtr]::Zero -or
        [PitchRateSmoke]::IsWindowEnabled($addPlayerTeam)) {
        throw "The add-player dialog did not lock the team selected by the match line-up."
    }
    $addTeamIndex = [PitchRateSmoke]::SendMessage(
        $addPlayerTeam, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $addTeamData = [PitchRateSmoke]::SendMessage(
        $addPlayerTeam, 0x0150, [IntPtr]$addTeamIndex, [IntPtr]::Zero).ToInt64()
    if ($addTeamData -ne $homeTeamData) {
        throw "The add-player dialog opened for the wrong team."
    }
    $addFirst = [PitchRateSmoke]::FindChildById($addPlayerDialog, 2100)
    $addLast = [PitchRateSmoke]::FindChildById($addPlayerDialog, 2101)
    $addSave = [PitchRateSmoke]::FindChildById($addPlayerDialog, 2105)
    [PitchRateSmoke]::SendMessageText($addFirst, 0x000C, [IntPtr]::Zero, "Smoke") | Out-Null
    [PitchRateSmoke]::SendMessageText($addLast, 0x000C, [IntPtr]::Zero, "Signing") | Out-Null
    [PitchRateSmoke]::PostMessage($addPlayerDialog, 0x0111, [IntPtr]2105, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the player added from the match dialog" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($recordDialog))
    }
    $homeLineupCountAfter = [PitchRateSmoke]::SendMessage(
        $homeLineupList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($homeLineupCountAfter -ne $homeLineupCount + 1) {
        throw "The player created from the match dialog was not added to that team's list."
    }
    Write-Host "Add player from match dialog OK"

    [PitchRateSmoke]::PostMessage($recordDialog, 0x0111, [IntPtr]2212, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the second add-player dialog from the match dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($recordDialog))
    }
    $addPlayerDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
    $addFirst = [PitchRateSmoke]::FindChildById($addPlayerDialog, 2100)
    $addLast = [PitchRateSmoke]::FindChildById($addPlayerDialog, 2101)
    [PitchRateSmoke]::SendMessageText($addFirst, 0x000C, [IntPtr]::Zero, "Smoke") | Out-Null
    [PitchRateSmoke]::SendMessageText($addLast, 0x000C, [IntPtr]::Zero, "Signing") | Out-Null
    [PitchRateSmoke]::PostMessage($addPlayerDialog, 0x0111, [IntPtr]2105, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the duplicate warning from the match dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "#32770")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            ([PitchRateSmoke]::WindowText($candidate) -like "Duplicate player*") -and
            -not [PitchRateSmoke]::IsWindowEnabled($addPlayerDialog))
    }
    $matchDuplicateDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "#32770")
    $addDuplicateButton = [PitchRateSmoke]::FindChildButtonByText(
        $matchDuplicateDialog, "Add duplicate")
    if ($addDuplicateButton -eq [IntPtr]::Zero) {
        throw "The duplicate warning from the match dialog is missing its add choice."
    }
    [PitchRateSmoke]::SendMessage($addDuplicateButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the duplicate player added from the match dialog" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "#32770") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($recordDialog))
    }
    $homeLineupCountAfterDuplicate = [PitchRateSmoke]::SendMessage(
        $homeLineupList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($homeLineupCountAfterDuplicate -ne $homeLineupCount + 2) {
        throw "The duplicate player confirmed from the match dialog was not added to the team's list."
    }
    Write-Host "Duplicate warning from match dialog OK"

    $awayTeamCombo = [PitchRateSmoke]::FindChildById($recordDialog, 2202)
    $awayTeamIndex = [PitchRateSmoke]::SendMessage(
        $awayTeamCombo, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $awayTeamData = [PitchRateSmoke]::SendMessage(
        $awayTeamCombo, 0x0150, [IntPtr]$awayTeamIndex, [IntPtr]::Zero).ToInt64()
    [PitchRateSmoke]::PostMessage($recordDialog, 0x0111, [IntPtr]2213, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the away add-player dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($recordDialog))
    }
    $addPlayerDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog")
    $addPlayerTeam = [PitchRateSmoke]::FindChildById($addPlayerDialog, 2102)
    $addTeamIndex = [PitchRateSmoke]::SendMessage(
        $addPlayerTeam, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $addTeamData = [PitchRateSmoke]::SendMessage(
        $addPlayerTeam, 0x0150, [IntPtr]$addTeamIndex, [IntPtr]::Zero).ToInt64()
    if ($addTeamData -ne $awayTeamData) {
        throw "The away add-player button opened the wrong team."
    }
    $addFirst = [PitchRateSmoke]::FindChildById($addPlayerDialog, 2100)
    $addLast = [PitchRateSmoke]::FindChildById($addPlayerDialog, 2101)
    [PitchRateSmoke]::SendMessageText($addFirst, 0x000C, [IntPtr]::Zero, "Smoke") | Out-Null
    [PitchRateSmoke]::SendMessageText($addLast, 0x000C, [IntPtr]::Zero, "Visitor") | Out-Null
    [PitchRateSmoke]::PostMessage($addPlayerDialog, 0x0111, [IntPtr]2105, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the away player added from the match dialog" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRatePlayerDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($recordDialog))
    }
    $awayLineupCountAfter = [PitchRateSmoke]::SendMessage(
        $awayLineupList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($awayLineupCountAfter -ne $awayLineupCount + 1) {
        throw "The player created from the away list was not added to that team's list."
    }
    Write-Host "Add player from away list OK"

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
    $editButton = [PitchRateSmoke]::FindChildById($detailsDialog, 2303)
    if ($editButton -eq [IntPtr]::Zero) {
        throw "The match-details dialog is missing its edit button."
    }
    [PitchRateSmoke]::PostMessage($detailsDialog, 0x0111, [IntPtr]2303, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the edit-match dialog" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            ([PitchRateSmoke]::WindowText($candidate) -eq "Edit match - PitchRate") -and
            -not [PitchRateSmoke]::IsWindowEnabled($detailsDialog))
    }
    $editDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
    $editHomeTeam = [PitchRateSmoke]::FindChildById($editDialog, 2201)
    $editAwayTeam = [PitchRateSmoke]::FindChildById($editDialog, 2202)
    $editHomeScore = [PitchRateSmoke]::FindChildById($editDialog, 2203)
    $editAwayScore = [PitchRateSmoke]::FindChildById($editDialog, 2204)
    $editLineup = [PitchRateSmoke]::FindChildById($editDialog, 2205)
    $editAwayLineup = [PitchRateSmoke]::FindChildById($editDialog, 2211)
    if ($editHomeTeam -eq [IntPtr]::Zero -or $editAwayTeam -eq [IntPtr]::Zero -or
        $editHomeScore -eq [IntPtr]::Zero -or $editAwayScore -eq [IntPtr]::Zero -or
        $editLineup -eq [IntPtr]::Zero -or $editAwayLineup -eq [IntPtr]::Zero) {
        throw "The edit-match dialog is missing fixture or line-up controls."
    }
    if ([PitchRateSmoke]::IsWindowEnabled($editHomeTeam) -or
        [PitchRateSmoke]::IsWindowEnabled($editAwayTeam)) {
        throw "The edit-match dialog allowed the fixture teams to change."
    }
    $homeSelection = [PitchRateSmoke]::SendMessage(
        $editHomeTeam, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $awaySelection = [PitchRateSmoke]::SendMessage(
        $editAwayTeam, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($homeSelection -lt 0 -or $awaySelection -lt 0 -or $homeSelection -eq $awaySelection) {
        throw "The edit-match dialog did not preselect the fixture teams."
    }
    $editLineupCount = [PitchRateSmoke]::SendMessage(
        $editLineup, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $editAwayLineupCount = [PitchRateSmoke]::SendMessage(
        $editAwayLineup, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($editLineupCount -lt 1 -or $editAwayLineupCount -lt 1) {
        throw "The edit-match dialog did not load both sides of the existing line-up."
    }
    $editDate = [PitchRateSmoke]::FindChildById($editDialog, 2200)
    if ($editDate -eq [IntPtr]::Zero -or
        [PitchRateSmoke]::ControlText($editDate) -notmatch '^\d{4}-\d{2}-\d{2}$') {
        throw "The edit-match dialog did not prefill the match date."
    }
    [PitchRateSmoke]::SendMessageText($editAwayScore, 0x000C, [IntPtr]::Zero, "2") | Out-Null
    if ([PitchRateSmoke]::ControlText($editAwayScore) -ne "2") {
        throw "The edit-match dialog did not accept the new score."
    }
    [PitchRateSmoke]::PostMessage($editDialog, 0x0111, [IntPtr]2208, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the edited match to save" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($detailsDialog) -and
            -not [PitchRateSmoke]::IsWindowEnabled($main))
    }
    [PitchRateSmoke]::PostMessage($detailsDialog, 0x0111, [IntPtr]2303, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the edited match to reopen" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            -not [PitchRateSmoke]::IsWindowEnabled($detailsDialog))
    }
    $editDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
    $editAwayScore = [PitchRateSmoke]::FindChildById($editDialog, 2204)
    if ($editAwayScore -eq [IntPtr]::Zero) {
        throw "The reopened edit dialog is missing its away-score control."
    }
    $restoredAwayScore = [PitchRateSmoke]::ControlText($editAwayScore)
    if ($restoredAwayScore -ne "2") {
        throw "The edited score was not restored from the saved match (got '$restoredAwayScore')."
    }
    [PitchRateSmoke]::PostMessage($editDialog, 0x0111, [IntPtr]2209, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the edit-match dialog to cancel" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($detailsDialog))
    }
    Write-Host "Edit match flow OK"
    [PitchRateSmoke]::PostMessage($detailsDialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the details dialog to close" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDetailsDialog") -eq [IntPtr]::Zero)
    }
    Wait-Until -Description "the owner to be re-enabled" -Condition {
        return [PitchRateSmoke]::IsWindowEnabled($main)
    }
    $matchesPage = [PitchRateSmoke]::FindChildByClass($main, "PitchRatePage", $true)
    $pageEditButton = [PitchRateSmoke]::FindChildById($matchesPage, 1303)
    if ($pageEditButton -eq [IntPtr]::Zero -or
        -not [PitchRateSmoke]::IsWindowEnabled($pageEditButton)) {
        throw "The match list did not enable Edit match for the selected match."
    }
    [PitchRateSmoke]::PostMessage($matchesPage, 0x0111, [IntPtr]1303, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the match-list edit command" -Condition {
        $candidate = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
        return ($candidate -ne [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowVisible($candidate) -and
            ([PitchRateSmoke]::WindowText($candidate) -eq "Edit match - PitchRate") -and
            -not [PitchRateSmoke]::IsWindowEnabled($main))
    }
    $editDialog = [PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog")
    [PitchRateSmoke]::PostMessage($editDialog, 0x0111, [IntPtr]2209, [IntPtr]::Zero) | Out-Null
    Wait-Until -Description "the match-list edit dialog to cancel" -Condition {
        return ([PitchRateSmoke]::FindTopLevel([uint32]$process.Id, "PitchRateMatchDialog") -eq [IntPtr]::Zero -and
            [PitchRateSmoke]::IsWindowEnabled($main))
    }
    Write-Host "Match-list edit command OK"
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
