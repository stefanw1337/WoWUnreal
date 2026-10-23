param(
    # Optional custom background; default is the original scene with its logo removed.
    [string]$ImagePath = '',
    [Parameter(Mandatory = $true)]
    [string]$ReadyFile,
    [string]$IconPath = '',
    [string]$ProgressFile = '',
    [string]$ClientPidFile = '',
    [int]$TimeoutSeconds = 600,
    [string]$PreviewOutput = ''
)

Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName WindowsBase

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class WowUnrealWindowGate
{
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    public static extern int SetCurrentProcessExplicitAppUserModelID(string appId);

    private delegate bool EnumWindowsProc(IntPtr window, IntPtr state);
    private delegate void WinEventProc(IntPtr hook, uint eventType, IntPtr window,
        int objectId, int childId, uint eventThread, uint eventTime);

    private static IntPtr showEventHook = IntPtr.Zero;
    private static WinEventProc showEventCallback;

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr state);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr window, out Rect rect);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    private static extern bool BringWindowToTop(IntPtr window);

    [DllImport("user32.dll")]
    private static extern IntPtr SetActiveWindow(IntPtr window);

    [DllImport("user32.dll")]
    private static extern IntPtr SetFocus(IntPtr window);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentThreadId();

    [DllImport("user32.dll")]
    private static extern bool AttachThreadInput(uint attachThread, uint attachToThread, bool attach);

    [DllImport("user32.dll")]
    private static extern bool AllowSetForegroundWindow(uint processId);

    [DllImport("user32.dll")]
    private static extern bool IsIconic(IntPtr window);

    [DllImport("user32.dll")]
    private static extern bool ShowWindowAsync(IntPtr window, int command);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter,
        int x, int y, int width, int height, uint flags);

    [DllImport("user32.dll")]
    private static extern IntPtr SetWinEventHook(uint eventMin, uint eventMax,
        IntPtr module, WinEventProc callback, uint processId, uint threadId, uint flags);

    [DllImport("user32.dll")]
    private static extern bool UnhookWinEvent(IntPtr hook);

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    private static List<IntPtr> FindWindows(int processId, bool visibleOnly)
    {
        var result = new List<IntPtr>();
        EnumWindows((window, state) =>
        {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == processId && (!visibleOnly || IsWindowVisible(window)))
            {
                result.Add(window);
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static void HideProcessWindows(int processId)
    {
        foreach (IntPtr window in FindWindows(processId, true))
        {
            ShowWindow(window, 0); // SW_HIDE
        }
    }

    public static void BeginHidingNewProcessWindows(int processId)
    {
        EndHidingNewProcessWindows();
        showEventCallback = (hook, eventType, window, objectId, childId, eventThread, eventTime) =>
        {
            if (window != IntPtr.Zero && objectId == 0)
            {
                ShowWindow(window, 0); // SW_HIDE before DWM presents the first frame
            }
        };
        const uint EVENT_OBJECT_SHOW = 0x8002;
        const uint WINEVENT_OUTOFCONTEXT = 0;
        showEventHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
            IntPtr.Zero, showEventCallback, (uint)processId, 0, WINEVENT_OUTOFCONTEXT);
        HideProcessWindows(processId);
    }

    public static void EndHidingNewProcessWindows()
    {
        if (showEventHook != IntPtr.Zero)
        {
            UnhookWinEvent(showEventHook);
            showEventHook = IntPtr.Zero;
        }
        showEventCallback = null;
    }

    public static bool ShowLargestProcessWindow(int processId)
    {
        IntPtr largest = IntPtr.Zero;
        long largestArea = -1;
        foreach (IntPtr window in FindWindows(processId, false))
        {
            Rect bounds;
            if (!GetWindowRect(window, out bounds))
            {
                continue;
            }
            long width = Math.Max(0, bounds.Right - bounds.Left);
            long height = Math.Max(0, bounds.Bottom - bounds.Top);
            long area = width * height;
            if (area > largestArea)
            {
                largestArea = area;
                largest = window;
            }
        }

        if (largest != IntPtr.Zero)
        {
            const int SW_SHOW = 5;
            const int SW_RESTORE = 9;
            const uint SWP_NOSIZE = 0x0001;
            const uint SWP_NOMOVE = 0x0002;
            const uint SWP_SHOWWINDOW = 0x0040;
            IntPtr HWND_TOP = IntPtr.Zero;
            IntPtr HWND_TOPMOST = new IntPtr(-1);
            IntPtr HWND_NOTOPMOST = new IntPtr(-2);

            // Windows normally rejects a foreground request made by a hidden
            // launcher after another application owned focus during startup.
            // First make the real client visible, then temporarily join the
            // relevant input queues for one explicit hand-off. The window is
            // raised to HWND_TOP, not TOPMOST, so normal Alt-Tab behaviour is
            // restored immediately after launch.
            AllowSetForegroundWindow((uint)processId);
            ShowWindowAsync(largest, IsIconic(largest) ? SW_RESTORE : SW_SHOW);
            SetWindowPos(largest, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

            uint currentThread = GetCurrentThreadId();
            uint targetProcess;
            uint targetThread = GetWindowThreadProcessId(largest, out targetProcess);
            IntPtr foreground = GetForegroundWindow();
            uint foregroundProcess = 0;
            uint foregroundThread = foreground == IntPtr.Zero
                ? 0
                : GetWindowThreadProcessId(foreground, out foregroundProcess);
            bool attachedForeground = foregroundThread != 0 && foregroundThread != currentThread &&
                AttachThreadInput(currentThread, foregroundThread, true);
            bool attachedTarget = targetThread != 0 && targetThread != currentThread &&
                AttachThreadInput(currentThread, targetThread, true);
            try
            {
                SetWindowPos(largest, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                SetWindowPos(largest, HWND_NOTOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                BringWindowToTop(largest);
                SetWindowPos(largest, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                SetForegroundWindow(largest);
                SetActiveWindow(largest);
                SetFocus(largest);
            }
            finally
            {
                if (attachedTarget)
                {
                    AttachThreadInput(currentThread, targetThread, false);
                }
                if (attachedForeground)
                {
                    AttachThreadInput(currentThread, foregroundThread, false);
                }
            }
            return true;
        }
        return false;
    }
}
'@

# Give this WPF window its own taskbar identity instead of inheriting the
# PowerShell host's icon/group. The background scene is preserved separately.
[WowUnrealWindowGate]::SetCurrentProcessExplicitAppUserModelID('WowUnreal.Launcher') | Out-Null
$window = New-Object System.Windows.Window
$window.WindowStyle = [System.Windows.WindowStyle]::None
$window.ResizeMode = [System.Windows.ResizeMode]::NoResize
$window.ShowInTaskbar = $true
$window.Topmost = $true
$window.ShowActivated = $false
$window.Focusable = $false
$window.WindowStartupLocation = [System.Windows.WindowStartupLocation]::CenterScreen
$window.Background = [System.Windows.Media.Brushes]::Black
$window.Title = 'WowUnreal'

# The approved project W/U icon is independent of the logo-free splash scene.
if (-not $IconPath) {
    $IconPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'Content/Splash/WowUnrealProjectIcon.png'
}
$appIcon = New-Object System.Windows.Media.Imaging.BitmapImage
$appIcon.BeginInit()
$appIcon.CacheOption = [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
$appIcon.UriSource = New-Object System.Uri($IconPath, [System.UriKind]::Absolute)
$appIcon.EndInit()
$appIcon.Freeze()
$window.Icon = $appIcon

if (-not $ImagePath) {
    $ImagePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'Content/Splash/WowUnrealSplashNoLogo.png'
}
# Load the background before sizing the window so its native aspect ratio is
# preserved instead of stretching or cropping the artwork.
$bitmap = $null
if (Test-Path -LiteralPath $ImagePath) {
    $bitmap = New-Object System.Windows.Media.Imaging.BitmapImage
    $bitmap.BeginInit()
    $bitmap.CacheOption = [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
    $bitmap.UriSource = New-Object System.Uri($ImagePath, [System.UriKind]::Absolute)
    $bitmap.EndInit()
}

# WPF sizes are device-independent pixels, while Windows may apply additional
# display scaling. Derive a compact window from the usable work area so the
# splash cannot extend beyond the screen at 125-250% DPI or on small displays.
$workArea = [System.Windows.SystemParameters]::WorkArea
$imageAspect = 1.5
if ($bitmap -and $bitmap.PixelHeight -gt 0) {
    $imageAspect = $bitmap.PixelWidth / [double]$bitmap.PixelHeight
}
$targetWidth = [Math]::Min(760.0, $workArea.Width * 0.72)
$targetHeight = $targetWidth / $imageAspect
if ($targetHeight -gt ($workArea.Height * 0.72)) {
    $targetHeight = $workArea.Height * 0.72
    $targetWidth = $targetHeight * $imageAspect
}
$window.Width = [Math]::Max(420.0, $targetWidth)
$window.Height = $window.Width / $imageAspect
$window.MaxWidth = [Math]::Max(420.0, $workArea.Width - 32.0)
$window.MaxHeight = [Math]::Max($window.Height, $workArea.Height - 32.0)

$root = New-Object System.Windows.Controls.Grid

if ($bitmap) {
    $image = New-Object System.Windows.Controls.Image
    $image.Source = $bitmap
    $image.Stretch = [System.Windows.Media.Stretch]::Uniform
    $root.Children.Add($image) | Out-Null
}

$overlay = New-Object System.Windows.Controls.Border
$overlay.Background = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromArgb(205, 5, 7, 10))
$overlay.BorderBrush = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromArgb(220, 145, 118, 55))
$overlay.BorderThickness = New-Object System.Windows.Thickness(1)
$overlay.HorizontalAlignment = [System.Windows.HorizontalAlignment]::Stretch
$overlay.VerticalAlignment = [System.Windows.VerticalAlignment]::Bottom
$overlay.Height = 14
$overlay.Margin = New-Object System.Windows.Thickness(48, 0, 48, 18)
$overlay.Padding = New-Object System.Windows.Thickness(2)

$progress = New-Object System.Windows.Controls.ProgressBar
$progress.Height = 10
$progress.Minimum = 0
$progress.Maximum = 100
$progress.IsIndeterminate = $false
$progress.Value = 0
$progress.Foreground = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromRgb(42, 125, 216))
$progress.Background = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromRgb(24, 30, 40))
$overlay.Child = $progress
$root.Children.Add($overlay) | Out-Null

$window.Content = $root

# Render the actual WPF tree without showing a window or starting the client.
if ($PreviewOutput) {
    $previewHeight = [int][Math]::Round(760 / $imageAspect)
    $progress.Value = 70
    $root.Measure((New-Object System.Windows.Size(760, $previewHeight)))
    $root.Arrange((New-Object System.Windows.Rect(0, 0, 760, $previewHeight)))
    $root.UpdateLayout()
    $preview = New-Object System.Windows.Media.Imaging.RenderTargetBitmap(760, $previewHeight, 96, 96, [System.Windows.Media.PixelFormats]::Pbgra32)
    $preview.Render($root)
    $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($preview))
    $stream = [System.IO.File]::Create($PreviewOutput)
    try { $encoder.Save($stream) } finally { $stream.Dispose() }
    return
}

$startedAt = [DateTime]::UtcNow
$transitionState = [PSCustomObject]@{
    HookedClientPid = 0
    ReadySeenAt = $null
    RevealClientPid = 0
}
$timer = New-Object System.Windows.Threading.DispatcherTimer
$timer.Interval = New-Object System.TimeSpan(0, 0, 0, 0, 25)
$timer.Add_Tick({
    $elapsed = ([DateTime]::UtcNow - $startedAt).TotalSeconds
    $clientProcessId = 0
    if ($ClientPidFile -and (Test-Path -LiteralPath $ClientPidFile)) {
        $rawClientPid = Get-Content -LiteralPath $ClientPidFile -Raw -ErrorAction SilentlyContinue
        [Int32]::TryParse($rawClientPid.Trim(), [ref]$clientProcessId) | Out-Null
    }

    if ($clientProcessId -gt 0 -and $transitionState.HookedClientPid -ne $clientProcessId) {
        [WowUnrealWindowGate]::BeginHidingNewProcessWindows($clientProcessId)
        $transitionState.HookedClientPid = $clientProcessId
        # StartWowUnrealClient creates Unreal suspended. Acknowledge only after
        # the per-process EVENT_OBJECT_SHOW hook is active, then the launcher can
        # resume it without exposing the initial empty/black native window.
        if ($ClientPidFile) {
            Set-Content -LiteralPath ($ClientPidFile + '.windowgate') -Value 'armed' -Encoding Ascii
        }
    }

    # Unreal creates its game window early and normally displays a black frame
    # while the glue scene initializes. Keep every window owned by this exact
    # client process hidden until the ready marker is written.
    if ($clientProcessId -gt 0 -and -not (Test-Path -LiteralPath $ReadyFile)) {
        [WowUnrealWindowGate]::HideProcessWindows($clientProcessId)
    }

    $reportedProgress = 0.0
    if ($ProgressFile -and (Test-Path -LiteralPath $ProgressFile)) {
        $rawProgress = Get-Content -LiteralPath $ProgressFile -Raw -ErrorAction SilentlyContinue
        $parsedProgress = 0.0
        if ([Double]::TryParse($rawProgress.Trim(), [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$parsedProgress)) {
            $reportedProgress = [Math]::Max(0.0, [Math]::Min(100.0, $parsedProgress))
        }
    }

    # The launcher owns these milestones; do not invent an indeterminate phase.
    # The final ready marker is the authoritative signal that the client is ready.
    if (Test-Path -LiteralPath $ReadyFile) {
        $reportedProgress = 100.0
        if ($null -eq $transitionState.ReadySeenAt) {
            $transitionState.ReadySeenAt = [DateTime]::UtcNow
        }
    }
    $progress.Value = $reportedProgress

    if ($null -ne $transitionState.ReadySeenAt) {
        # The ready marker is written on the game thread before that Slate frame
        # necessarily reaches DWM. Keep the real window hidden for a short render
        # hand-off. Close the splash before revealing the game window so the
        # topmost splash cannot cover the foreground activation.
        $readyAge = ([DateTime]::UtcNow - $transitionState.ReadySeenAt).TotalMilliseconds
        if ($readyAge -lt 650 -and $clientProcessId -gt 0) {
            [WowUnrealWindowGate]::HideProcessWindows($clientProcessId)
        }
        elseif ($clientProcessId -gt 0) {
            $transitionState.RevealClientPid = $clientProcessId
            $timer.Stop()
            $window.Close()
        }
    }
    elseif ($elapsed -ge $TimeoutSeconds) {
        $transitionState.RevealClientPid = $clientProcessId
        $timer.Stop()
        $window.Close()
    }
})
$window.Add_Closed({
    if ($timer.IsEnabled) { $timer.Stop() }
    [WowUnrealWindowGate]::EndHidingNewProcessWindows()
    $window.Dispatcher.InvokeShutdown()
})
$window.Add_Closing({
    if ($transitionState.RevealClientPid -le 0 -and $transitionState.HookedClientPid -gt 0) {
        $transitionState.RevealClientPid = $transitionState.HookedClientPid
    }
})

$window.Show()
$timer.Start()
[System.Windows.Threading.Dispatcher]::Run()

if ($transitionState.RevealClientPid -gt 0) {
    [WowUnrealWindowGate]::ShowLargestProcessWindow($transitionState.RevealClientPid) | Out-Null
}

if (Test-Path -LiteralPath $ReadyFile) {
    Remove-Item -LiteralPath $ReadyFile -Force -ErrorAction SilentlyContinue
}
if ($ProgressFile -and (Test-Path -LiteralPath $ProgressFile)) {
    Remove-Item -LiteralPath $ProgressFile -Force -ErrorAction SilentlyContinue
}
if ($ClientPidFile -and (Test-Path -LiteralPath $ClientPidFile)) {
    Remove-Item -LiteralPath $ClientPidFile -Force -ErrorAction SilentlyContinue
}
if ($ClientPidFile -and (Test-Path -LiteralPath ($ClientPidFile + '.windowgate'))) {
    Remove-Item -LiteralPath ($ClientPidFile + '.windowgate') -Force -ErrorAction SilentlyContinue
}
