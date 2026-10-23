using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

// Five completed client milestones. The last step is the native ready marker.
internal sealed class StartupProgress : Control
{
    int completed;
    internal void SetCompleted(int value) { completed = Math.Max(completed, Math.Min(5, value)); Invalidate(); }
    internal StartupProgress()
    {
        SetStyle(ControlStyles.SupportsTransparentBackColor | ControlStyles.UserPaint |
            ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        BackColor = Color.Transparent;
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        if (Width < 3 || Height < 3) return;
        using (var background = new SolidBrush(Color.FromArgb(205, 5, 7, 10)))
        using (var fill = new System.Drawing.Drawing2D.LinearGradientBrush(ClientRectangle,
            Color.FromArgb(28, 78, 145), Color.FromArgb(75, 176, 232), 0f))
        using (var border = new Pen(Color.FromArgb(220, 145, 118, 55)))
        {
            e.Graphics.FillRectangle(background, ClientRectangle);
            e.Graphics.FillRectangle(fill, 1, 1, (Width - 2) * completed / 5, Height - 2);
            e.Graphics.DrawRectangle(border, 0, 0, Width - 1, Height - 1);
        }
    }
}

internal sealed class GameExitException : Exception
{
    internal GameExitException(string message) : base(message) { }
}

internal sealed class StartupSplash : Form
{
    Image artwork;
    readonly StartupProgress progress = new StartupProgress();
    internal void SetCompleted(int value) { progress.SetCompleted(value); }
    internal StartupSplash()
    {
        Text = "WowUnreal";
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.CenterScreen;
        int width = Math.Min(760, Screen.PrimaryScreen.WorkingArea.Width - 80);
        ClientSize = new Size(width, width * 3 / 4);
        BackColor = Color.FromArgb(21, 38, 49);
        DoubleBuffered = true;
        string image = Path.Combine(Client.Root, "Launcher", "Splash.png");
        if (!File.Exists(image)) throw new FileNotFoundException("Splash artwork missing. Keep the Launcher artwork folder beside Launcher.exe.", image);
        artwork = Image.FromFile(image);
        BackgroundImage = artwork;
        BackgroundImageLayout = ImageLayout.Zoom;
        progress.SetBounds(48, ClientSize.Height - 32, ClientSize.Width - 96, 14);
        Controls.Add(progress);
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing && artwork != null) { BackgroundImage = null; artwork.Dispose(); artwork = null; }
        base.Dispose(disposing);
    }
}

internal static class StartupPresentation
{
    internal static void Preview(string path)
    {
        using (var splash = new StartupSplash())
        {
            splash.ShowInTaskbar = false; splash.Opacity = 0;
            splash.StartPosition = FormStartPosition.Manual; splash.Location = new Point(-30000, -30000);
            splash.Show(); Application.DoEvents();
            using (var bitmap = new Bitmap(splash.Width, splash.Height))
            {
                splash.DrawToBitmap(bitmap, new Rectangle(Point.Empty, bitmap.Size));
                bitmap.Save(path, System.Drawing.Imaging.ImageFormat.Png);
            }
            splash.Close();
        }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
    static int NativeExitCode(IntPtr process)
    {
        uint code;
        if (!GetExitCodeProcess(process, out code)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        return unchecked((int)code);
    }

    internal static void Run(ProcessStartInfo start, string[] args)
    {
        bool hidden = args.Any(a => a.Equals("-RenderOffscreen", StringComparison.OrdinalIgnoreCase));
        string state = Path.Combine(Client.Root, "WowUnreal", "Saved", "Launcher");
        Directory.CreateDirectory(state);
        string ready = Path.Combine(state, Guid.NewGuid().ToString("N") + ".ready");
        string log = Path.Combine(state, "last-startup.log");
        Action<string> record = message => File.AppendAllText(log, DateTime.UtcNow.ToString("O") + " " + message + Environment.NewLine);
        File.WriteAllText(log, "");
        start.Arguments += " " + Client.Quote("-splashreadyfile=" + ready);
        var info = new WowUnrealNativeLauncher.StartupInfo();
        info.cb = Marshal.SizeOf(info); info.flags = 1; info.showWindow = 0;
        var child = new WowUnrealNativeLauncher.ProcessInformation();
        Process process = null;
        bool resumed = false;
        DateTime started = DateTime.UtcNow;
        try
        {
            WowUnrealWindowGate.SetCurrentProcessExplicitAppUserModelID("WowUnreal.Launcher");
            using (var splash = new StartupSplash())
            {
                if (!hidden) { splash.Show(); splash.Refresh(); Application.DoEvents(); }
                record(hidden ? "Splash suppressed for offscreen verification." : "Splash displayed.");
                if (!WowUnrealNativeLauncher.CreateProcess(start.FileName,
                    new StringBuilder(Client.Quote(start.FileName) + " " + start.Arguments),
                    IntPtr.Zero, IntPtr.Zero, false, 4, IntPtr.Zero, start.WorkingDirectory, ref info, out child))
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                process = Process.GetProcessById(child.processId);
                WowUnrealWindowGate.BeginHidingNewProcessWindows(child.processId);
                record("Window gate installed before resuming game. PID=" + child.processId);
                if (WowUnrealNativeLauncher.ResumeThread(child.thread) == UInt32.MaxValue)
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                resumed = true;
                var elapsed = Stopwatch.StartNew();
                double readyAt = -1;
                // A slow disk or large asset pack is not a readiness signal.
                // Keep the splash until the client reports ready or exits.
                while (!process.HasExited)
                {
                    Application.DoEvents();
                    if (!hidden && splash.IsDisposed) { process.Kill(); throw new OperationCanceledException("Startup cancelled."); }
                    WowUnrealWindowGate.HideProcessWindows(child.processId);
                    int stage;
                    try
                    {
                        string progressFile = ready + ".progress";
                        if (File.Exists(progressFile) && Int32.TryParse(File.ReadAllText(progressFile), out stage))
                            splash.SetCompleted(Math.Max(0, Math.Min(4, stage)));
                    }
                    catch (IOException) { } // Native milestone update is atomic; tolerate a brief file race.
                    if (readyAt < 0 && File.Exists(ready))
                    { readyAt = elapsed.Elapsed.TotalSeconds; splash.SetCompleted(5); record("Login ready marker received."); }
                    // Give Slate the same present grace period as the existing launcher.
                    if (readyAt >= 0 && elapsed.Elapsed.TotalSeconds - readyAt >= 0.65) break;
                    Thread.Sleep(25);
                }
                WowUnrealWindowGate.EndHidingNewProcessWindows();
                splash.Close();
                if (!process.HasExited)
                {
                    if (!hidden) WowUnrealWindowGate.ShowLargestProcessWindow(child.processId);
                    record("Startup handoff complete.");
                }
                else record("Game exited during startup. Exit code=" + NativeExitCode(child.process));
            }
            process.WaitForExit();
            int exitCode = NativeExitCode(child.process);
            record("Game exited. Exit code=" + exitCode);
            if (exitCode != 0)
            {
                string detail = DescribeExit(start.FileName, started, exitCode);
                record(detail);
                throw new GameExitException(detail);
            }
        }
        finally
        {
            WowUnrealWindowGate.EndHidingNewProcessWindows();
            if (process != null)
            {
                if (!process.HasExited)
                {
                    if (!resumed || hidden) process.Kill();
                    else WowUnrealWindowGate.ShowLargestProcessWindow(child.processId);
                }
                process.Dispose();
            }
            if (child.thread != IntPtr.Zero) WowUnrealNativeLauncher.CloseHandle(child.thread);
            if (child.process != IntPtr.Zero) WowUnrealNativeLauncher.CloseHandle(child.process);
            foreach (string marker in new[] { ready, ready + ".progress", ready + ".progress.tmp" })
                try { if (File.Exists(marker)) File.Delete(marker); } catch (IOException) { }
        }
    }
    static string DescribeExit(string executable, DateTime started, int exitCode)
    {
        bool closing = false;
        string module = null;
        string gameLog = Path.Combine(Client.Root, "WowUnreal", "Saved", "Logs", "WowUnreal.log");
        try
        {
            if (File.Exists(gameLog) && File.GetLastWriteTimeUtc(gameLog) >= started)
                using (var stream = new FileStream(gameLog, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                {
                    stream.Seek(Math.Max(0, stream.Length - 65536), SeekOrigin.Begin);
                    using (var reader = new StreamReader(stream))
                    {
                        string tail = reader.ReadToEnd();
                        closing = tail.Contains("LogExit: Exiting.") && tail.Contains("Log file closed");
                    }
                }
            // Optional evidence only: never infer the culprit merely from an installed DLL.
            using (var events = new EventLog("Application"))
            {
                int count = events.Entries.Count;
                for (int i = count - 1; i >= Math.Max(0, count - 64); --i)
                {
                    var entry = events.Entries[i];
                    if (entry.TimeGenerated.ToUniversalTime() < started) break;
                    if ((entry.InstanceId & 0xffff) != 1000) continue;
                    string[] fields = entry.ReplacementStrings;
                    if (!fields.Any(x => String.Equals(x, executable, StringComparison.OrdinalIgnoreCase))) continue;
                    module = fields.FirstOrDefault(x => x.EndsWith(".dll", StringComparison.OrdinalIgnoreCase));
                    break;
                }
            }
        }
        catch (Exception) { } // Missing event-log access must not replace the actual exit error.
        return (closing ? "The game finished shutting down, but a component then reported an error" : "The game exited unexpectedly") +
            " (0x" + unchecked((uint)exitCode).ToString("X8") + ")." +
            (module == null ? "" : "\nWindows reports the fault in: " + Path.GetFileName(module) + ".") +
            "\nSee WowUnreal/Saved/Logs and Saved/Launcher/last-startup.log for details.";
    }

}


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
