param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath,
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,
    [Parameter(Mandatory = $true)]
    [string]$WowDataPath,
    [Parameter(Mandatory = $true)]
    [string]$ReadyFile,
    [Parameter(Mandatory = $true)]
    [string]$ClientPidFile,
    [switch]$OfflineFlow,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArguments
)

function ConvertTo-NativeArgument([string]$Value) {
    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    # All launcher-owned paths are normalized without a trailing slash. This
    # standard Windows quoting is therefore sufficient for the UE command line.
    return '"' + $Value.Replace('"', '\"') + '"'
}

# ProcessStartInfo.WindowStyle is only advisory for GUI applications. Unreal can
# therefore show its first empty HWND before the splash process learns its PID.
# Create the client suspended with STARTF_USESHOWWINDOW/SW_HIDE instead: the
# splash gets time to install its per-process window hook before Unreal executes
# any code capable of creating or displaying a window.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class WowUnrealNativeLauncher
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct StartupInfo
    {
        public int cb;
        public string reserved;
        public string desktop;
        public string title;
        public int x;
        public int y;
        public int xSize;
        public int ySize;
        public int xCountChars;
        public int yCountChars;
        public int fillAttribute;
        public int flags;
        public short showWindow;
        public short reserved2;
        public IntPtr reserved2Data;
        public IntPtr stdInput;
        public IntPtr stdOutput;
        public IntPtr stdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ProcessInformation
    {
        public IntPtr process;
        public IntPtr thread;
        public int processId;
        public int threadId;
    }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool CreateProcess(
        string applicationName,
        StringBuilder commandLine,
        IntPtr processAttributes,
        IntPtr threadAttributes,
        bool inheritHandles,
        uint creationFlags,
        IntPtr environment,
        string currentDirectory,
        ref StartupInfo startupInfo,
        out ProcessInformation processInformation);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint ResumeThread(IntPtr thread);

    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

$clientArguments = @(
    $ProjectPath,
    '/Game/Maps/WowWorld',
    '-game',
    '-LiveCoding=false',
    # File logging stays enabled. -log creates an extra console HWND that can
    # flash at the launcher's 70% milestone before the hide hook catches it.
    '-nosplash',
    '-dx12',
    '-DDC-ForceMemoryCache',
    "-splashreadyfile=$ReadyFile",
    '-testscene=login',
    '-ExecCmds=r.Shadow.Virtual.Enable 0,r.VolumetricFog 0,r.Fog 1,r.Lumen.DiffuseIndirect.Allow 0,r.Lumen.Reflections.Allow 0'
)

# The dist tools and these legacy shortcuts share the same settings once
# dist is present. The game reads DataSource from that INI at startup.
$portableSettings = Join-Path (Split-Path -Parent $ProjectPath) 'dist\Settings\GameUserSettings.ini'
if (Test-Path -LiteralPath $portableSettings) {
    $clientArguments += "-GameUserSettingsINI=$portableSettings"
} else {
    $clientArguments += "-wowdata=$WowDataPath"
}

if ($OfflineFlow) {
    $clientArguments += '-offlineflow'
}
if ($ExtraArguments) {
    $clientArguments += $ExtraArguments
}

$nativeArguments = (($clientArguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
$nativeCommandLine = New-Object System.Text.StringBuilder(
    ((ConvertTo-NativeArgument $ExecutablePath) + ' ' + $nativeArguments))
$startup = New-Object WowUnrealNativeLauncher+StartupInfo
$startup.cb = [Runtime.InteropServices.Marshal]::SizeOf($startup)
$startup.flags = 1 # STARTF_USESHOWWINDOW
$startup.showWindow = 0 # SW_HIDE
$processInfo = New-Object WowUnrealNativeLauncher+ProcessInformation
$created = [WowUnrealNativeLauncher]::CreateProcess(
    $ExecutablePath,
    $nativeCommandLine,
    [IntPtr]::Zero,
    [IntPtr]::Zero,
    $false,
    4, # CREATE_SUSPENDED
    [IntPtr]::Zero,
    (Split-Path -Parent $ProjectPath),
    [ref]$startup,
    [ref]$processInfo)
if (-not $created) {
    $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "The Unreal client process could not be started (Win32 error $nativeError)."
}

$windowGateFile = $ClientPidFile + '.windowgate'
try {
    Set-Content -LiteralPath $ClientPidFile -Value $processInfo.processId -Encoding Ascii

    # The splash normally acknowledges within one 25 ms timer tick. Keep this
    # bounded so a failed splash cannot leave the Unreal process suspended.
    $gateDeadline = [DateTime]::UtcNow.AddSeconds(5)
    while (-not (Test-Path -LiteralPath $windowGateFile) -and
        [DateTime]::UtcNow -lt $gateDeadline) {
        Start-Sleep -Milliseconds 10
    }

    $resumeResult = [WowUnrealNativeLauncher]::ResumeThread($processInfo.thread)
    if ($resumeResult -eq [UInt32]::MaxValue) {
        $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "The Unreal client thread could not be resumed (Win32 error $nativeError)."
    }

    $client = [System.Diagnostics.Process]::GetProcessById($processInfo.processId)
    $client.WaitForExit()
    $exitCode = $client.ExitCode
    $client.Dispose()
    exit $exitCode
}
finally {
    if ($processInfo.thread -ne [IntPtr]::Zero) {
        [WowUnrealNativeLauncher]::CloseHandle($processInfo.thread) | Out-Null
    }
    if ($processInfo.process -ne [IntPtr]::Zero) {
        [WowUnrealNativeLauncher]::CloseHandle($processInfo.process) | Out-Null
    }
}
