using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Device.Location;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using System.Web.Script.Serialization;

// Both executables use this file. The game and this editor share the actual UE
// GameUserSettings.ini, including repeated keybinding entries and unknown keys.
internal sealed class Ini
{
    internal readonly List<string> Lines;
    internal Ini(string text) { Lines = text.Replace("\r", "").Split('\n').ToList(); }
    internal static Ini Load(string path) { return new Ini(File.Exists(path) ? File.ReadAllText(path) : ""); }
    static bool Section(string line) { return line.StartsWith("[") && line.EndsWith("]"); }
    internal string Get(string section, string key, string fallback)
    {
        string active = "", result = fallback;
        foreach (string raw in Lines)
        {
            string line = raw.Trim();
            if (Section(line)) active = line.Substring(1, line.Length - 2);
            else if (String.Equals(active, section, StringComparison.OrdinalIgnoreCase))
            {
                int eq = line.IndexOf('=');
                if (eq > 0 && String.Equals(line.Substring(0, eq).Trim(), key, StringComparison.OrdinalIgnoreCase))
                    result = line.Substring(eq + 1).Trim().Trim('"');
            }
        }
        return result;
    }
    internal void Set(string section, string key, string value)
    {
        if (value.IndexOfAny(new[] {'\r', '\n'}) >= 0) throw new ArgumentException("Setting must be a single line.");
        string active = ""; int insert = -1;
        for (int i = 0; i < Lines.Count; ++i)
        {
            string line = Lines[i].Trim();
            if (Section(line))
            {
                if (String.Equals(active, section, StringComparison.OrdinalIgnoreCase) && insert < 0) insert = i;
                active = line.Substring(1, line.Length - 2);
            }
            else if (String.Equals(active, section, StringComparison.OrdinalIgnoreCase))
            {
                int eq = line.IndexOf('=');
                if (eq > 0 && String.Equals(line.Substring(0, eq).Trim(), key, StringComparison.OrdinalIgnoreCase))
                { Lines[i] = key + "=" + value; return; }
            }
        }
        if (insert < 0 && String.Equals(active, section, StringComparison.OrdinalIgnoreCase)) insert = Lines.Count;
        if (insert < 0) { Lines.Add(""); Lines.Add("[" + section + "]"); insert = Lines.Count; }
        Lines.Insert(insert, key + "=" + value);
    }
    internal void Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temp, String.Join("\r\n", Lines), new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temp, path, path + ".bak");
            else File.Move(temp, path);
        }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }
}

internal static class Client
{
    internal const string Video = "/Script/Engine.GameUserSettings";
    internal const string Prefs = "WowUnreal.ClientPreferences";
    internal static readonly string Root = AppDomain.CurrentDomain.BaseDirectory;
    // Everything that belongs to the player lives in User\ and survives every build.
    internal static readonly string UserRoot = PrepareUserRoot();
    internal static readonly string SettingsPath = Path.Combine(UserRoot, "Settings", "GameUserSettings.ini");
    static string PrepareUserRoot()
    {
        string user = Path.Combine(Root, "User");
        try
        {
            Directory.CreateDirectory(user);
            // One-time move from the old flat layout (Settings, Interface ... beside Launcher.exe).
            foreach (string name in new[] { "Settings", "Interface", "Data", "Sky", "Sound", "UserSounds", "Screenshot", "Logs" })
            {
                string from = Path.Combine(Root, name), to = Path.Combine(user, name);
                try { if (Directory.Exists(from) && !Directory.Exists(to)) Directory.Move(from, to); }
                catch (Exception) { /* a locked folder stays where it is; the game falls back to it */ }
            }
        }
        catch (Exception) { }
        return user;
    }
    internal static string SessionName
    {
        get { using (var sha = SHA256.Create()) return "Local\\WowUnreal-" + BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(Root.ToUpperInvariant()))).Replace("-", ""); }
    }
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr value);

    [STAThread]
    static int Main(string[] args)
    {
        try
        {
            if (args.Length >= 2 && args[0] == "--self-test") { SelfTest(args[1]); return 0; }
            if (args.Length >= 2 && args[0] == "--validate") { Validate(args[1]); return 0; }
            if (args.Length >= 4 && args[0] == "--initialize") { Initialize(args[1], args[2], args[3]); return 0; }
            try { SetProcessDpiAwarenessContext(new IntPtr(-4)); } catch (EntryPointNotFoundException) { SetProcessDPIAware(); }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            if (args.Length >= 2 && args[0] == "--render-splash-preview") { StartupPresentation.Preview(args[1]); return 0; }
            if (args.Length >= 2 && args[0] == "--render-preview")
            {
                using (var form = new SettingsWindow(true))
                using (var bitmap = new Bitmap(form.Width, form.Height))
                {
                    form.ShowInTaskbar = false; form.Opacity = 0;
                    form.StartPosition = FormStartPosition.Manual; form.Location = new Point(-30000, -30000);
                    form.Show(); if (args.Length >= 3) form.SelectPreviewTab(args[2]); Application.DoEvents(); form.PerformLayout();
                    form.DrawToBitmap(bitmap, new Rectangle(0, 0, form.Width, form.Height));
                    bitmap.Save(args[1], System.Drawing.Imaging.ImageFormat.Png);
                    form.Close();
                }
                return 0;
            }
#if SETTINGS_APP
            Application.Run(new SettingsWindow());
#else
            if (args.Any(a => a.Equals("-settings", StringComparison.OrdinalIgnoreCase))) Application.Run(new SettingsWindow());
            else Launch(args);
#endif
            return 0;
        }
        catch (Exception ex)
        {
            if (args.Length >= 2 && (args[0] == "--self-test" || args[0] == "--validate")) { File.WriteAllText(args[1], "FAIL\r\n" + ex); return 1; }
            if (args.Length >= 2 && args[0] == "--render-preview") { File.WriteAllText(args[1] + ".error.txt", ex.ToString()); return 1; }
            if (args.Length >= 1 && args[0] == "--initialize") { File.WriteAllText(Path.Combine(Root, "initialize-error.log"), ex.ToString()); return 1; }
            if (args.Any(a => a.Equals("-RenderOffscreen", StringComparison.OrdinalIgnoreCase))) { File.WriteAllText(Path.Combine(Root, "startup-error.log"), ex.ToString()); return 1; }
            MessageBox.Show(ex.Message + (ex is GameExitException ? "" : "\n\nUse WowUnrealSettings.exe to check the data folder and settings."), "WowUnreal", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }
    internal static string Quote(string value)
    {
        // Windows CommandLineToArgvW quoting, including trailing backslashes.
        var b = new StringBuilder("\""); int slashes = 0;
        foreach (char c in value)
        {
            if (c == '\\') { ++slashes; continue; }
            b.Append('\\', slashes * (c == '"' ? 2 : 1)); slashes = 0;
            if (c == '"') b.Append('\\'); b.Append(c);
        }
        b.Append('\\', slashes * 2); return b.Append('"').ToString();
    }
    internal static string DataFolder(string raw)
    {
        string value = Environment.ExpandEnvironmentVariables(raw.Trim().Trim('"'));
        if (String.IsNullOrWhiteSpace(value)) throw new InvalidOperationException("Select your World of Warcraft 3.3.5a Data folder in Settings first.");
        value = Path.GetFullPath(value);
        if (File.Exists(Path.Combine(value, "Data", "common.MPQ"))) value = Path.Combine(value, "Data");
        if (!File.Exists(Path.Combine(value, "common.MPQ"))) throw new InvalidOperationException("common.MPQ was not found in the selected Data folder.");
        return value.TrimEnd(Path.DirectorySeparatorChar);
    }
    /// <summary>Per-server data: any client Data folder with MPQ archives (Vanilla has no common.MPQ).</summary>
    internal static string ServerDataFolder(string raw)
    {
        string value = Path.GetFullPath(Environment.ExpandEnvironmentVariables(raw.Trim().Trim('"')));
        if (Directory.Exists(Path.Combine(value, "Data")) && Directory.GetFiles(Path.Combine(value, "Data"), "*.mpq").Length > 0) value = Path.Combine(value, "Data");
        if (!Directory.Exists(value) || Directory.GetFiles(value, "*.mpq").Length == 0) throw new InvalidOperationException("No MPQ archives found in " + value + ". Choose the server's WoW Data folder.");
        return value.TrimEnd(Path.DirectorySeparatorChar);
    }
    internal static void SetDisplay(Ini ini, int width, int height, int mode)
    {
        if (width < 800 || height < 600 || width > 16384 || height > 16384 || mode < 0 || mode > 2)
            throw new InvalidOperationException("Choose a valid resolution and display mode.");
        ini.Set(Video, "Version", "5");
        foreach (string key in new[] {"ResolutionSizeX", "LastUserConfirmedResolutionSizeX", "DesiredScreenWidth", "LastUserConfirmedDesiredScreenWidth"}) ini.Set(Video, key, width.ToString());
        foreach (string key in new[] {"ResolutionSizeY", "LastUserConfirmedResolutionSizeY", "DesiredScreenHeight", "LastUserConfirmedDesiredScreenHeight"}) ini.Set(Video, key, height.ToString());
        ini.Set(Video, "FullscreenMode", mode.ToString());
        ini.Set(Video, "LastConfirmedFullscreenMode", mode.ToString());
        ini.Set(Video, "PreferredFullscreenMode", mode == 0 ? "0" : "1");
    }
    static void Initialize(string project, string engine, string dataFolder)
    {
        string profiles = Path.Combine(Path.GetDirectoryName(SettingsPath), "WowCredentials.json");
        if (!File.Exists(profiles))
        {
            string projectRoot = Path.GetDirectoryName(project);
            // A package update preserves the destination. Prefer already-shared
            // profiles, then the pre-standalone stores; retain every source.
            string[] previousStores = {
                Path.Combine(projectRoot, "dist", "User", "Settings", "WowCredentials.json"),
                Path.Combine(projectRoot, "dist", "Settings", "WowCredentials.json"),
                Path.Combine(Root, "WowUnreal", "Saved", "WowCredentials.json"),
                Path.Combine(projectRoot, "Saved", "WowCredentials.json")
            };
            foreach (string previous in previousStores)
            {
                if (!File.Exists(previous)) continue;
                Directory.CreateDirectory(Path.GetDirectoryName(profiles));
                File.Copy(previous, profiles, false);
                break;
            }
        }
        string pathsFile = Path.Combine(Root, "ClientPaths.ini");
        if (!File.Exists(pathsFile))
        {
            var paths = new Ini("; Development launcher paths. No WoW assets are distributed here.\n");
            paths.Set("Development", "Project", project);
            paths.Set("Development", "EngineExecutable", engine);
            paths.Save(pathsFile);
        }
        if (!File.Exists(SettingsPath))
        {
            string legacy = Path.Combine(Path.GetDirectoryName(project), "Saved", "Config", "WindowsEditor", "GameUserSettings.ini");
            var settings = Ini.Load(legacy);
            // First setup uses the user's requested 1080p window. Preserve all
            // existing bindings/preferences; subsequent builds never reset it.
            SetDisplay(settings, 1920, 1080, 2);
            if (!String.IsNullOrWhiteSpace(dataFolder)) settings.Set(Prefs, "DataSource", DataFolder(dataFolder));
            settings.Save(SettingsPath);
        }
    }
    internal static string ProfilesPath { get { return Path.Combine(Path.GetDirectoryName(SettingsPath), "WowCredentials.json"); } }
    /// <summary>Saved server profiles written by the client (all fields kept, including sealed passwords).</summary>
    internal static List<Dictionary<string, object>> LoadProfiles()
    {
        var result = new List<Dictionary<string, object>>();
        if (!File.Exists(ProfilesPath)) return result;
        var items = new JavaScriptSerializer().DeserializeObject(File.ReadAllText(ProfilesPath, Encoding.UTF8)) as object[];
        if (items != null) foreach (object item in items) { var profile = item as Dictionary<string, object>; if (profile != null) result.Add(profile); }
        return result;
    }
    internal static void SaveProfiles(List<Dictionary<string, object>> profiles)
    {
        string temp = ProfilesPath + ".tmp";
        File.WriteAllText(temp, new JavaScriptSerializer().Serialize(profiles.ToArray()), new UTF8Encoding(false));
        if (File.Exists(ProfilesPath)) File.Replace(temp, ProfilesPath, ProfilesPath + ".bak"); else File.Move(temp, ProfilesPath);
    }
    internal static string ProfileText(Dictionary<string, object> profile, string key)
    {
        object value; return profile.TryGetValue(key, out value) && value != null ? Convert.ToString(value, CultureInfo.InvariantCulture) : "";
    }
    /// <summary>Profiles of one server share a data folder: address, port and game version.</summary>
    internal static string ServerKey(Dictionary<string, object> profile)
    {
        string version = ProfileText(profile, "game_version"); if (version == "") version = "2";
        return ProfileText(profile, "server").Trim().ToLowerInvariant() + ":" + ProfileText(profile, "port") + "|" + version;
    }
    internal static string PackagedClient { get { return Path.Combine(Root, "WowUnreal", "Binaries", "Win64", "WowUnreal.exe"); } }
    static string[] Arguments(string[] user, string project)
    {
        var result = new List<string>();
        if (!String.IsNullOrEmpty(project)) result.AddRange(new[] {project, "/Game/Maps/WowWorld", "-game", "-LiveCoding=false"});
        result.AddRange(new[] {"-nosplash", "-dx12", "-testscene=login", "-GameUserSettingsINI=" + SettingsPath});
        result.AddRange(user);
        return result.ToArray();
    }
    static void Launch(string[] args)
    {
        using (var gate = new Mutex(false, SessionName))
        {
            bool acquired; try { acquired = gate.WaitOne(0); } catch (AbandonedMutexException) { acquired = true; }
            if (!acquired) throw new InvalidOperationException("This client or its Settings window is already running.");
            try
            {
                var paths = Ini.Load(Path.Combine(Root, "ClientPaths.ini"));
                string engine = paths.Get("Development", "EngineExecutable", "");
                string project = paths.Get("Development", "Project", "");
                if (File.Exists(PackagedClient)) { engine = PackagedClient; project = null; }
                else if (!File.Exists(engine) || !File.Exists(project)) throw new InvalidOperationException("The client files are missing. Keep the Engine and WowUnreal folders beside Launcher.exe.");
                DataFolder(Ini.Load(SettingsPath).Get(Prefs, "DataSource", ""));
                var start = new ProcessStartInfo(engine, String.Join(" ", Arguments(args, project).Select(Quote)))
                { UseShellExecute = false, WorkingDirectory = project == null ? Path.GetDirectoryName(engine) : Path.GetDirectoryName(project) };
                StartupPresentation.Run(start, args);
            }
            finally { gate.ReleaseMutex(); }
        }
    }
    static void Validate(string report)
    {
        var paths = Ini.Load(Path.Combine(Root, "ClientPaths.ini"));
        bool packaged = File.Exists(PackagedClient);
        if (!packaged && (!File.Exists(paths.Get("Development", "EngineExecutable", "")) || !File.Exists(paths.Get("Development", "Project", "")))) throw new Exception("Client files missing");
        var ini = Ini.Load(SettingsPath);
        DataFolder(ini.Get(Prefs, "DataSource", ""));
        File.WriteAllText(report, (packaged ? "PASS: packaged standalone client and external Data found; no Editor/project required.\r\n" : "PASS: development engine/project/data found.\r\n") + SettingsPath);
    }
    static void SelfTest(string report)
    {
        string dir = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(report)), "settings-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        string file = Path.Combine(dir, "GameUserSettings.ini");
        var ini = new Ini("; keep comment\n[Bindings]\nProfileV2=ONE|W|\nProfileV2=TWO|S|\n[Unknown]\nKey=untouched\n");
        SetDisplay(ini, 1920, 1080, 2);
        ini.Set(Prefs, "DataSource", "C:\\Spill med mellomrom\\ÆØÅ\\Data");
        ini.Save(file);
        var second = Ini.Load(file);
        if (second.Get(Video, "FullscreenMode", "") != "2" || second.Get(Video, "ResolutionSizeX", "") != "1920") throw new Exception("Display persistence failed");
        if (second.Lines.Count(x => x.StartsWith("ProfileV2=")) != 2 || second.Get("Unknown", "Key", "") != "untouched") throw new Exception("Unrelated data lost");
        if (!second.Get(Prefs, "DataSource", "").Contains("ÆØÅ")) throw new Exception("Unicode path lost");
        SetDisplay(second, 3840, 2160, 1); second.Save(file);
        if (!File.Exists(file + ".bak") || Ini.Load(file + ".bak").Get(Video, "ResolutionSizeX", "") != "1920") throw new Exception("Atomic backup failed");
        var launch = Arguments(new[] { "-offline" }, "E:\\Project with spaces\\WowUnreal.uproject");
        if (!launch.Contains("-offline") || !launch.Any(x => x.StartsWith("-GameUserSettingsINI=")) || launch.Any(x => x.StartsWith("-resx"))) throw new Exception("Arguments override settings");
        var standalone = Arguments(new[] { "-offline" }, null);
        if (standalone.Contains("-game") || standalone.Any(x => x.EndsWith(".uproject")) || !standalone.Contains("-offline")) throw new Exception("Standalone must not depend on an Editor project");
        if (Quote("a b\\") != "\"a b\\\\\"") throw new Exception("Trailing slash quoting failed");
        bool rejected = false; try { SetDisplay(second, -1, 1080, 2); } catch (InvalidOperationException) { rejected = true; }
        if (!rejected) throw new Exception("Invalid display accepted");
        File.WriteAllText(report, "PASS: settings reopen, Unicode/spaces, repeated bindings, unknown keys, atomic backup, offline/shared-INI arguments, quoting, invalid resolution rejection.\r\nFixtures: " + dir);
    }
}

internal sealed class SettingsWindow : Form
{
    readonly TextBox data = new TextBox();
    readonly ComboBox resolution = new ComboBox();
    readonly ComboBox mode = new ComboBox();
    readonly TextBox raw = new TextBox();
    readonly List<Action<Ini>> writers = new List<Action<Ini>>();
    Action saveProfiles;
    Func<bool> profilesPending; // a server's Data folder differs from WowCredentials.json
    DataGridView serverGrid;
    readonly Label status = new Label();
    Mutex gate;
    bool ownsGate, rawEdited, loading = true;
    GeoCoordinateWatcher locationWatcher;
    System.Windows.Forms.Timer locationTimeout;
    Button locateButton;
    readonly bool preview;
    protected override bool ShowWithoutActivation { get { return preview || base.ShowWithoutActivation; } }

    internal SettingsWindow(bool preview = false)
    {
        this.preview = preview;
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
        Text = "WowUnreal Settings"; ClientSize = new Size(720, 700); MinimumSize = new Size(650, 550);
        StartPosition = FormStartPosition.CenterScreen; AutoScaleMode = AutoScaleMode.Dpi;
        Font = new Font("Segoe UI", 10);
        var ini = Ini.Load(Client.SettingsPath);
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 4, ColumnCount = 1, Padding = new Padding(16) };
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 52)); layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 50)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 45));
        Controls.Add(layout);
        layout.Controls.Add(new Label { Text = "Settings are shared with the client. Close the client before editing.\nSelect your local WoW Data folder before starting the client.", Dock = DockStyle.Fill }, 0, 0);
        var tabs = new TabControl { Dock = DockStyle.Fill }; layout.Controls.Add(tabs, 0, 1);
        var general = Page(tabs, "Data / Display");
        data.Text = ini.Get(Client.Prefs, "DataSource", "");
        Row(general, "WoW 3.3.5a Data folder", data);
        var browse = new Button { Text = "Browse…", AutoSize = true };
        browse.Click += delegate { using (var picker = new FolderBrowserDialog { Description = "Choose your local WoW 3.3.5a Data folder", SelectedPath = data.Text }) if (picker.ShowDialog(this) == DialogResult.OK) data.Text = picker.SelectedPath; };
        Row(general, "", browse);
        var locale = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        string savedLocale = ini.Get(Client.Prefs, "Locale", "Auto");
        Action refreshLocales = delegate {
            string previous = locale.SelectedItem as string ?? savedLocale;
            locale.Items.Clear(); locale.Items.Add("Auto (WoW Config.wtf / available language)");
            string root = data.Text.Trim();
            if (Directory.Exists(Path.Combine(root, "Data"))) root = Path.Combine(root, "Data");
            foreach (string code in new[] { "enUS", "enGB", "deDE", "frFR", "esES", "esMX", "ruRU", "koKR", "zhCN", "zhTW" })
                if (Directory.Exists(Path.Combine(root, code))) locale.Items.Add(code);
            locale.SelectedIndex = locale.Items.Contains(previous) ? locale.Items.IndexOf(previous) : 0;
        };
        refreshLocales();
        data.TextChanged += delegate { refreshLocales(); };
        Row(general, "Language / Locale", locale);
        Row(general, "", new Label { Text = "Uses installed language folders. Applies after restarting the client.\nCustom / HD packs remain enabled. Some Unreal menus are English only.", AutoSize = true });
        writers.Add(delegate(Ini settings) { settings.Set(Client.Prefs, "Locale", locale.SelectedIndex > 0 ? (string)locale.SelectedItem : "Auto"); });
        mode.DropDownStyle = ComboBoxStyle.DropDownList; mode.Items.AddRange(new object[] { "Fullscreen", "Borderless (desktop size)", "Windowed" });
        int m; mode.SelectedIndex = Int32.TryParse(ini.Get(Client.Video, "FullscreenMode", "2"), out m) && m >= 0 && m <= 2 ? m : 2;
        Row(general, "Display mode", mode);
        resolution.Items.AddRange(new object[] { "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160" });
        resolution.Text = ini.Get(Client.Video, "ResolutionSizeX", "1920") + " x " + ini.Get(Client.Video, "ResolutionSizeY", "1080");
        resolution.TextChanged += delegate { if (!loading && mode.SelectedIndex == 1) mode.SelectedIndex = 2; };
        Row(general, "Window resolution", resolution);
        Row(general, "", new Label { Text = "Selecting a resolution switches borderless to windowed.\nUse render scale for lower rendering resolution in borderless.", AutoSize = true });
        Bool(general, ini, Client.Video, "bUseVSync", "VSync", true);
        Number(general, ini, Client.Video, "FrameRateLimit", "FPS limit (0 = unlimited)", 60, 0, 1000);
        Number(general, ini, Client.Prefs, "PreferredRefreshRate", "Preferred refresh / FPS", 60, 0, 1000);
        var servers = Page(tabs, "Servers");
        BuildServersPage(servers);
        var video = Page(tabs, "Graphics");
        Number(video, ini, "ScalabilityGroups", "sg.ResolutionQuality", "Render scale (%)", 100, 50, 150);
        foreach (string q in new[] {"ViewDistance", "AntiAliasing", "Shadow", "GlobalIllumination", "Reflection", "PostProcess", "Texture", "Effects", "Foliage", "Shading", "Landscape"})
            Number(video, ini, "ScalabilityGroups", "sg." + q + "Quality", q + " quality (0–4)", 2, 0, 4);
        Choice(video, ini, "AntiAliasingMethod", "Anti-aliasing", new[] {"Off", "FXAA", "TAA", "TSR", "AMD FSR"}, 3);
        Choice(video, ini, "FsrQualityMode", "FSR quality", new[] {"Native AA", "Quality", "Balanced", "Performance", "Ultra performance"}, 1);
        foreach (string key in new[] { "DepthOfField", "MotionBlur", "FrameGeneration", "DarkerRealisticLighting", "PerformanceTimings", "PerformanceRenderingStatus" }) Bool(video, ini, Client.Prefs, key, key, false);
        Number(video, ini, Client.Prefs, "DepthOfFieldStrength", "Depth of Field strength (0.5–4)", 2, 0.5m, 4, 1);
        Bool(video, ini, Client.Prefs, "VolumetricFog", "Volumetric fog", false);
        Bool(video, ini, Client.Prefs, "Lumen", "Lumen global illumination", false);
        Bool(video, ini, Client.Prefs, "HardwareRayTracing", "Hardware ray tracing", false);
        Number(video, ini, Client.Prefs, "Gamma", "Gamma (1.6–3.0)", 2.2m, 1.6m, 3m, 2);
        var weather = Page(tabs, "Weather");
        var weatherSource = Choice(weather, ini, "WeatherSource", "Weather source", new[] {"Server (zone simulation offline)", "Real weather adapted to zone", "Zone simulation"}, 0);
        var locationEnabled = Bool(weather, ini, Client.Prefs, "WeatherLocationConfigured", "Use this location for real weather", false);
        var latitude = Number(weather, ini, Client.Prefs, "WeatherLatitude", "Latitude", 0, -90, 90, 5);
        var longitude = Number(weather, ini, Client.Prefs, "WeatherLongitude", "Longitude", 0, -180, 180, 5);
        locateButton = new Button { Text = "Use my location (Windows)", AutoSize = true };
        locateButton.Click += delegate { FindLocation(latitude, longitude, locationEnabled, weatherSource); };
        Row(weather, "Detect location", locateButton);
        var privacy = new Button { Text = "Windows location permissions", AutoSize = true };
        privacy.Click += delegate { try { Process.Start(new ProcessStartInfo("ms-settings:privacy-location") { UseShellExecute = true }); } catch (Exception ex) { status.Text = ex.Message; } };
        Row(weather, "Location access", privacy);
        Row(weather, "Real weather", new Label { Text = "Use Windows location or enter coordinates manually.\nWindows controls access; location is requested only on click.\nPress Save to store the location on this computer.\nThe game sends the saved coordinates to Open-Meteo.com.\nWeather data: Open-Meteo.com (CC BY 4.0), every 15 minutes.\nRestart the game to apply. Update location here when travelling.\nSnow in cold zones; dust in dry zones.", AutoSize = true });
        Slider(weather, ini, "DistanceFog", "Distance fog (100% = as in WoW, 0 = off)", 1m, 0m, 2m);
        Bool(weather, ini, Client.Prefs, "weatherlens", "Rain and melting snow on camera", true);
        Bool(weather, ini, Client.Prefs, "wetlens", "Wet camera lens", true);
        Bool(weather, ini, Client.Prefs, "frostlens", "Frost on camera lens", false);
        Row(weather, "Moon", new Label { Text = "The moon phase follows today's real date.\nFor a photographic moon, save an equirectangular moon colour map\n(e.g. NASA SVS \"CGI Moon Kit\", public domain) as User\\Sky\\Moon.jpg\n(beside Launcher.exe). Restart the game to apply.\nMoon and sun size: in game, Options > Experimental.", AutoSize = true });
        var audio = Page(tabs, "Sound");
        foreach (string key in new[] { "MasterVolume", "MusicVolume", "SfxVolume", "AmbienceVolume" }) Number(audio, ini, Client.Prefs, key, key + " (0–1)", key == "MasterVolume" ? 1 : 0.5m, 0, 1, 2);
        Bool(audio, ini, Client.Prefs, "soundenabled", "Enable Sound", true);
        Bool(audio, ini, Client.Prefs, "soundeffects", "Sound Effects", true);
        Bool(audio, ini, Client.Prefs, "musicenabled", "Music", true);
        Bool(audio, ini, Client.Prefs, "loopmusic", "Loop Music", true);
        Bool(audio, ini, Client.Prefs, "ambientenabled", "Ambient Sounds", true);
        Bool(audio, ini, Client.Prefs, "soundinbackground", "Sound in Background", false);
        var ui = Page(tabs, "Interface");
        Choice(ui, ini, "CursorMode", "Mouse cursor", new[] {"Hardware", "Software"}, 0);
        Row(ui, "", new Label { Text = "Both modes use the WoW metal glove and interaction icons.\nRestart the client after saving.", AutoSize = true });
        Slider(ui, ini, "CameraMouseSensitivity", "Camera sensitivity (left/right drag)", 1m, .25m, 3m);
        Slider(ui, ini, "MouseCursorScale", "Metal glove size", 1m, .5m, 2m);
        Number(ui, ini, Client.Prefs, "UIScale", "UI scale", 1, 0.75m, 1.5m, 2);
        Choice(ui, ini, "NameplateMode", "Nameplates", new[] {"All", "None", "Quest NPCs", "Interactable NPCs"}, 0);
        foreach (string key in new[] { "autolootdefault", "lootundermouse", "showtargetoftarget", "playerstatustext", "statustextpercentage", "actionbar3", "actionbar4", "actionbar5", "actionbar6", "alwaysshowactionbars", "FirstPersonImmersion", "FirstPersonHelmetView", "NpcAndPlayerCollision" })
            Bool(ui, ini, Client.Prefs, key, key, key == "showtargetoftarget" || key == "playerstatustext" || key.StartsWith("actionbar"));
        Number(ui, ini, Client.Prefs, "TooltipScale", "Tooltip scale", 1, 0.5m, 1.5m, 2);
        // Same names and defaults as the client's world options (WowClientPreferences.cpp).
        foreach (var option in new[] {
            Tuple.Create("tooltipatcursor", "Tooltip at the cursor", false),
            Tuple.Create("alwayscompareitems", "Always compare items", false),
            Tuple.Create("showcooldowntime", "Show cooldown numbers", false),
            Tuple.Create("instantquesttext", "Instant quest text", true),
            Tuple.Create("autoquestwatch", "Track newly accepted quests", true),
            Tuple.Create("autotrackzonequests", "Track every quest of the current zone", false),
            Tuple.Create("autosellgrey", "Sell grey items at vendors", false),
            Tuple.Create("autorepair", "Repair everything at armorers", false),
            Tuple.Create("chatbubbles", "Chat bubbles", true),
            Tuple.Create("chatbubblesparty", "Party chat bubbles", false),
            Tuple.Create("floatingcombattextdamage", "Floating combat text: damage", true),
            Tuple.Create("combattextsum", "Combat text: sum numbers", false),
            Tuple.Create("nameplateshowenemies", "Nameplates: enemies", true),
            Tuple.Create("nameplateshowfriends", "Nameplates: friends", true),
            Tuple.Create("unitnamenpc", "Names: all NPCs", false),
            Tuple.Create("unitnameinteractablenpc", "Names: interactable NPCs", true),
            Tuple.Create("unitnamemonster", "Names: monsters", true),
            Tuple.Create("unitnamefriendlyplayername", "Names: friendly players", true),
            Tuple.Create("unitnameenemyplayername", "Names: enemy players", true),
            Tuple.Create("unitnamenoncombatcreaturename", "Names: critters and companions", true),
            // r388 (Stefan 16.10: every game option here too, so a bad one can be switched off).
            Tuple.Create("showquestlevel", "Show quest level", true),
            Tuple.Create("showallspellranks", "Spellbook: show all spell ranks", true),
            Tuple.Create("nativequesttracker", "Classic native objective tracker", false),
            Tuple.Create("holdtorecast", "Held action key casts again when ready", true),
            Tuple.Create("combattextsumheals", "Combat text: sum heals received", false),
            Tuple.Create("combattextresource", "Combat text: resource gains", true),
            Tuple.Create("moviesubtitles", "Movie subtitles", true),
            Tuple.Create("showhelm", "Show helm", true),
            Tuple.Create("showcloak", "Show cloak", true),
            Tuple.Create("headlook", "Character looks at target / camera direction", true),
            Tuple.Create("satelliterecord", "Record Satellite v2 map tiles while playing", true) })
            Bool(ui, ini, Client.Prefs, option.Item1, option.Item2, option.Item3);
        var experimental = Page(tabs, "Experimental");
        Row(experimental, "", new Label { Text = "The same switches as Options > Experimental in game.\nIf a test leaves the game unusable, turn it off here.\nVR always starts off: switch it on in game (Ctrl+Shift+V or Options).", AutoSize = true });
        foreach (var option in new[] {
            Tuple.Create("vrfirstperson", "VR: first person only", true),
            Tuple.Create("hdcharacters", "HD character model", false),
            Tuple.Create("lootbeam", "Loot beams", false),
            Tuple.Create("questsparkles", "Sparkles on quest objects and gathering nodes", true),
            Tuple.Create("ambientweather", "Subtle zone atmosphere", true),
            Tuple.Create("rootedstandup", "Stand up before moving", false),
            Tuple.Create("realisticmoon", "Realistic moon rise/set", false),
            Tuple.Create("manualdof", "Manual depth of field (test)", false),
            // r388: the rest of Options > Experimental, same names and defaults as the game.
            Tuple.Create("sunrays", "Sun rays and lens flare", true),
            Tuple.Create("cumulusclouds", "Realistic cumulus clouds", true),
            Tuple.Create("shortrangeao", "Contact shadows (ambient occlusion)", true),
            Tuple.Create("portalculling", "Original room and portal visibility", true),
            Tuple.Create("hidekeepbackdrop", "Stone wall instead of the dark backdrop over Stormwind Keep", true),
            Tuple.Create("extremeweather", "Extreme weather", false),
            Tuple.Create("immersiveui", "Immersive interface", false),
            Tuple.Create("mountpreview", "Mount preview in the dressing room", true),
            Tuple.Create("noautoretaliate", "Do NOT attack back when attacked", false),
            Tuple.Create("hdnpcs", "NPC HD models", false),
            Tuple.Create("hdplayers", "Player HD models", false),
            Tuple.Create("vrmousepointer", "VR: aim with the mouse instead of the head", false),
            Tuple.Create("vrdwellclick", "VR: stare-to-click", false),
            Tuple.Create("stealthdetectrange", "Show enemy detection range while stealthed", false) })
            Bool(experimental, ini, Client.Prefs, option.Item1, option.Item2, option.Item3);
        {
            // Stealth look: one of four; the game stores it as three switches (none on = Shadow cloak).
            string[] lookKeys = { "stealthclassic", "stealthpredator", "stealthshadowonly" };
            var look = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
            look.Items.AddRange(new object[] { "Shadow cloak (default)", "Classic", "Predator", "Shadow only" });
            look.SelectedIndex = 0;
            for (int i = 0; i < lookKeys.Length; ++i)
            {
                bool on;
                if (Boolean.TryParse(ini.Get(Client.Prefs, lookKeys[i], "False"), out on) && on) { look.SelectedIndex = i + 1; break; }
            }
            Row(experimental, "Stealth look", look);
            writers.Add(x => { for (int i = 0; i < lookKeys.Length; ++i) x.Set(Client.Prefs, lookKeys[i], look.SelectedIndex == i + 1 ? "True" : "False"); });
        }
        {
            // r395: camera style, one at a time (VR always starts off): Normal / Isometric / Don't Starve.
            // Isometric = isometriccamera; Don't Starve = isometriccamera + isodontstarve.
            var camera = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
            camera.Items.AddRange(new object[] { "Normal (default)", "Isometric (Diablo-style)", "Don't Starve (top-down 2D look)" });
            bool iso, dontStarve;
            Boolean.TryParse(ini.Get(Client.Prefs, "isometriccamera", "False"), out iso);
            Boolean.TryParse(ini.Get(Client.Prefs, "isodontstarve", "False"), out dontStarve);
            camera.SelectedIndex = !iso ? 0 : dontStarve ? 2 : 1;
            Row(experimental, "Camera style", camera);
            writers.Add(x =>
            {
                x.Set(Client.Prefs, "isometriccamera", camera.SelectedIndex != 0 ? "True" : "False");
                x.Set(Client.Prefs, "isodontstarve", camera.SelectedIndex == 2 ? "True" : "False");
            });
        }
        Number(experimental, ini, Client.Prefs, "FocusTestDistance", "Manual focus distance (m)", 10, 0.5m, 100, 1);
        Number(experimental, ini, Client.Prefs, "FocusTestWidth", "Manual sharp region (0–1)", 0.5m, 0, 1, 2);
        Slider(experimental, ini, "MoonScale", "Moon size", 2.5m, 1m, 5m);
        Slider(experimental, ini, "SunScale", "Sun size", 1m, 1m, 10m);
        var advanced = new TabPage("All settings (INI)"); tabs.TabPages.Add(advanced);
        raw.Multiline = true; raw.MaxLength = 0; raw.ScrollBars = ScrollBars.Both; raw.WordWrap = false; raw.Font = new Font("Consolas", 10); raw.Dock = DockStyle.Fill;
        raw.Text = String.Join(Environment.NewLine, ini.Lines); raw.TextChanged += delegate { if (!loading) rawEdited = true; }; advanced.Controls.Add(raw);
        status.Dock = DockStyle.Fill; status.AutoEllipsis = true; status.Text = Client.SettingsPath; layout.Controls.Add(status, 0, 2);
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
        var close = new Button { Text = "Close", AutoSize = true }; close.Click += delegate { Close(); }; buttons.Controls.Add(close);
        var save = new Button { Text = "Save", AutoSize = true }; save.Click += delegate { Save(tabs.SelectedTab == advanced); }; buttons.Controls.Add(save);
        var safe = new Button { Text = "Safe video: 1080p", AutoSize = true };
        safe.Click += delegate { mode.SelectedIndex = 2; resolution.Text = "1920 x 1080"; tabs.SelectedIndex = 0; status.Text = "Safe display selected. Press Save to apply at next launch."; }; buttons.Controls.Add(safe);
        layout.Controls.Add(buttons, 0, 3);
        saveButton = save;
        gate = new Mutex(false, Client.SessionName);
        TryAcquireGate();
        if (!ownsGate) { save.Enabled = false; status.Text = "Waiting for the client to close... changes are kept and saved automatically once it has."; }
        // Live saving: every change is written within about a second. If the client (or
        // another Settings window) holds the settings, keep retrying until it closes.
        try { lastSavedText = BuildIniText(); } catch (Exception) { lastSavedText = null; }
        liveSave = new System.Windows.Forms.Timer { Interval = 1000 };
        liveSave.Tick += delegate { LiveSaveTick(); };
        if (!preview) liveSave.Start();
        loading = false;
    }
    Button saveButton;
    System.Windows.Forms.Timer liveSave;
    string lastSavedText;
    bool TryAcquireGate()
    {
        if (ownsGate) return true;
        try { ownsGate = gate.WaitOne(0); } catch (AbandonedMutexException) { ownsGate = true; }
        if (ownsGate && saveButton != null && !saveButton.IsDisposed) saveButton.Enabled = true;
        return ownsGate;
    }
    // Same result as Save on the normal tabs, as INI text, without writing anything.
    string BuildIniText()
    {
        var ini = Ini.Load(Client.SettingsPath);
        string[] parts = resolution.Text.ToLowerInvariant().Split('x'); int w, h;
        if (parts.Length != 2 || !Int32.TryParse(parts[0].Trim(), out w) || !Int32.TryParse(parts[1].Trim(), out h)) throw new InvalidOperationException("Enter a resolution such as 1920 x 1080.");
        if (mode.SelectedIndex == 1) { w = Screen.PrimaryScreen.Bounds.Width; h = Screen.PrimaryScreen.Bounds.Height; }
        Client.SetDisplay(ini, w, h, mode.SelectedIndex);
        ini.Set(Client.Prefs, "DataSource", Client.DataFolder(data.Text));
        foreach (var write in writers) write(ini);
        return String.Join("\r\n", ini.Lines);
    }
    void LiveSaveTick()
    {
        if (loading || rawEdited || IsDisposed) return;
        string text;
        try { text = BuildIniText(); }
        catch (Exception ex) { status.Text = ex.Message; return; }
        if (!TryAcquireGate())
        {
            if (text != lastSavedText) status.Text = "Waiting for the client to close... your changes will be saved automatically.";
            return;
        }
        // Server data folders save themselves when changed (no-op otherwise).
        try { if (saveProfiles != null) saveProfiles(); } catch (Exception ex) { LogSettings("Server profiles not saved: " + ex.Message); }
        if (text == lastSavedText) return;
        try
        {
            new Ini(text).Save(Client.SettingsPath);
            lastSavedText = text;
            status.Text = "Saved automatically " + DateTime.Now.ToString("HH:mm:ss") + " - the client uses these settings at its next launch.";
        }
        catch (Exception ex) { status.Text = "Could not save yet (" + ex.Message + "), retrying..."; LogSettings("Live save failed: " + ex); }
    }
    internal static void LogSettings(string line)
    {
        try
        {
            string dir = Path.Combine(Client.UserRoot, "Settings");
            Directory.CreateDirectory(dir);
            File.AppendAllText(Path.Combine(dir, "WowUnrealSettings.log"), DateTime.Now.ToString("s") + " " + line + "\r\n");
        }
        catch (Exception) { }
    }
    static string DistProcesses()
    {
        var names = new List<string>();
        foreach (var process in Process.GetProcesses())
        {
            try
            {
                string path = process.MainModule != null ? process.MainModule.FileName : null;
                if (path != null && path.StartsWith(Client.Root, StringComparison.OrdinalIgnoreCase) && process.Id != Process.GetCurrentProcess().Id)
                    names.Add(Path.GetFileName(path) + " (pid " + process.Id + ")");
            }
            catch (Exception) { }
            finally { process.Dispose(); }
        }
        return names.Count == 0 ? "none found" : String.Join(", ", names);
    }
    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (!preview && !rawEdited && liveSave != null)
        {
            liveSave.Stop();
            // A folder typed into the server list is only committed when the cell is left.
            if (serverGrid != null) serverGrid.EndEdit();
            string text = null;
            try { text = BuildIniText(); } catch (Exception ex) { LogSettings("Close: settings not valid: " + ex.Message); }
            bool serversPending = false;
            try { serversPending = profilesPending != null && profilesPending(); } catch (Exception) { }
            // Server Data folders are saved even when nothing else changed (they were lost before).
            if ((text != null && text != lastSavedText) || serversPending)
            {
                // Try for a few seconds (the client may just be shutting down).
                var until = DateTime.Now.AddSeconds(6);
                while (!TryAcquireGate() && DateTime.Now < until) { Application.DoEvents(); Thread.Sleep(250); }
                bool saved = false; string reason = "the client or another Settings window still holds the settings (open from this folder: " + DistProcesses() + ")";
                if (ownsGate)
                {
                    try
                    {
                        if (text != null && text != lastSavedText) { new Ini(text).Save(Client.SettingsPath); lastSavedText = text; }
                        if (saveProfiles != null) saveProfiles();
                        saved = true;
                    }
                    catch (Exception ex) { reason = ex.Message; }
                }
                if (!saved)
                {
                    LogSettings("Close: could not save - " + reason);
                    var answer = MessageBox.Show("Your changes could not be saved:\n" + reason + "\n\nClose anyway and lose them?\n(Details: Settings\\WowUnrealSettings.log)",
                        "WowUnreal Settings", MessageBoxButtons.YesNo, MessageBoxIcon.Warning);
                    if (answer != DialogResult.Yes) { e.Cancel = true; liveSave.Start(); return; }
                }
            }
        }
        base.OnFormClosing(e);
    }
    void BuildServersPage(TableLayoutPanel page)
    {
        List<Dictionary<string, object>> profiles;
        try { profiles = Client.LoadProfiles(); }
        catch (Exception ex) { Row(page, "Saved servers", new Label { Text = "Could not read WowCredentials.json: " + ex.Message, AutoSize = true }); return; }
        var grid = new DataGridView
        {
            AllowUserToAddRows = false, AllowUserToDeleteRows = false, AllowUserToResizeRows = false, RowHeadersVisible = false,
            AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill, SelectionMode = DataGridViewSelectionMode.CellSelect,
            MultiSelect = false, Height = 300, Dock = DockStyle.Top, BackgroundColor = SystemColors.Window, BorderStyle = BorderStyle.FixedSingle
        };
        grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "server", HeaderText = "Server", ReadOnly = true, FillWeight = 40 });
        grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "version", HeaderText = "Game", ReadOnly = true, FillWeight = 14 });
        grid.Columns.Add(new DataGridViewTextBoxColumn { Name = "data", HeaderText = "Data folder (empty = default)", FillWeight = 60 });
        grid.Columns.Add(new DataGridViewButtonColumn { Name = "browse", HeaderText = "", Text = "Browse…", UseColumnTextForButtonValue = true, FillWeight = 16 });
        grid.Columns.Add(new DataGridViewButtonColumn { Name = "clear", HeaderText = "", Text = "Default", UseColumnTextForButtonValue = true, FillWeight = 16 });
        var keys = new List<string>();
        foreach (var profile in profiles)
        {
            string key = Client.ServerKey(profile);
            if (keys.Contains(key)) { int existing = keys.IndexOf(key); if (Convert.ToString(grid.Rows[existing].Cells["data"].Value) == "") grid.Rows[existing].Cells["data"].Value = Client.ProfileText(profile, "data_path"); continue; }
            keys.Add(key);
            string alias = Client.ProfileText(profile, "alias"), address = Client.ProfileText(profile, "server") + ":" + Client.ProfileText(profile, "port");
            string version = Client.ProfileText(profile, "game_version");
            string versionName = version == "0" ? "Vanilla" : version == "1" ? "TBC" : "WotLK";
            grid.Rows.Add(String.Equals(alias, Client.ProfileText(profile, "server"), StringComparison.OrdinalIgnoreCase) ? address : alias + "  (" + address + ")", versionName, Client.ProfileText(profile, "data_path"));
        }
        grid.CellContentClick += delegate(object sender, DataGridViewCellEventArgs e)
        {
            if (e.RowIndex < 0) return;
            var cell = grid.Rows[e.RowIndex].Cells["data"];
            if (e.ColumnIndex == grid.Columns["clear"].Index) { cell.Value = ""; return; }
            if (e.ColumnIndex != grid.Columns["browse"].Index) return;
            using (var picker = new FolderBrowserDialog { Description = "Choose the WoW Data folder for this server", SelectedPath = Convert.ToString(cell.Value) })
                if (picker.ShowDialog(this) == DialogResult.OK) cell.Value = picker.SelectedPath;
        };
        serverGrid = grid;
        int row = page.RowCount++; page.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        page.Controls.Add(grid, 0, row); page.SetColumnSpan(grid, 2);
        grid.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
        Row(page, "", new Label { Text = profiles.Count == 0
            ? "No saved servers yet. Log in to a server once; it appears here."
            : "The login screen always uses the default Data folder (Data / Display tab).\nAfter logging in to a server with its own folder, the client switches to it\n(music, textures, models and addons) before the realm list.", AutoSize = true });
        profilesPending = delegate
        {
            for (int i = 0; i < keys.Count; ++i)
            {
                string raw = Convert.ToString(grid.Rows[i].Cells["data"].Value).Trim();
                string folder = raw == "" ? "" : Client.ServerDataFolder(raw);
                foreach (var profile in profiles)
                    if (Client.ServerKey(profile) == keys[i] && Client.ProfileText(profile, "data_path") != folder) return true;
            }
            return false;
        };
        saveProfiles = delegate
        {
            if (profiles.Count == 0) return;
            bool changed = false;
            for (int i = 0; i < keys.Count; ++i)
            {
                string raw = Convert.ToString(grid.Rows[i].Cells["data"].Value).Trim();
                string folder = raw == "" ? "" : Client.ServerDataFolder(raw);
                foreach (var profile in profiles)
                {
                    if (Client.ServerKey(profile) != keys[i] || Client.ProfileText(profile, "data_path") == folder) continue;
                    if (folder == "") profile.Remove("data_path"); else profile["data_path"] = folder;
                    changed = true;
                }
            }
            if (changed) Client.SaveProfiles(profiles);
        };
    }
    protected override void OnFormClosed(FormClosedEventArgs e) { StopLocation(); if (liveSave != null) { liveSave.Stop(); liveSave.Dispose(); } if (ownsGate) gate.ReleaseMutex(); if (gate != null) gate.Dispose(); base.OnFormClosed(e); }
    static TableLayoutPanel Page(TabControl tabs, string name)
    {
        var page = new TabPage(name); tabs.TabPages.Add(page);
        var rows = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 2, Padding = new Padding(10) };
        rows.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 46)); rows.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 54));
        page.AutoScroll = true; page.Controls.Add(rows); return rows;
    }
    static void Row(TableLayoutPanel table, string title, Control value)
    {
        int row = table.RowCount++; table.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        table.Controls.Add(new Label { Text = title, AutoSize = true, Padding = new Padding(0, 6, 0, 0), Margin = new Padding(3, 5, 3, 5) }, 0, row);
        value.Anchor = AnchorStyles.Left | AnchorStyles.Right; value.Margin = new Padding(3, 5, 3, 5); table.Controls.Add(value, 1, row);
    }
    CheckBox Bool(TableLayoutPanel rows, Ini ini, string section, string key, string title, bool fallback)
    {
        bool parsed; var box = new CheckBox { Checked = Boolean.TryParse(ini.Get(section, key, fallback.ToString()), out parsed) ? parsed : fallback };
        Row(rows, title, box); writers.Add(x => x.Set(section, key, box.Checked ? "True" : "False")); return box;
    }
    NumericUpDown Number(TableLayoutPanel rows, Ini ini, string section, string key, string title, decimal fallback, decimal min, decimal max, int digits = 0)
    {
        decimal n; if (!Decimal.TryParse(ini.Get(section, key, fallback.ToString(CultureInfo.InvariantCulture)), NumberStyles.Float, CultureInfo.InvariantCulture, out n)) n = fallback;
        var box = new NumericUpDown { Minimum = min, Maximum = max, DecimalPlaces = digits, Increment = digits > 0 ? 0.05m : 1, Value = Math.Max(min, Math.Min(max, n)) };
        Row(rows, title, box); writers.Add(x => x.Set(section, key, box.Value.ToString(CultureInfo.InvariantCulture))); return box;
    }
    void Slider(TableLayoutPanel rows, Ini ini, string key, string title, decimal fallback, decimal min, decimal max)
    {
        decimal value;
        if (!Decimal.TryParse(ini.Get(Client.Prefs, key, fallback.ToString(CultureInfo.InvariantCulture)), NumberStyles.Float, CultureInfo.InvariantCulture, out value)) value = fallback;
        var panel = new TableLayoutPanel { AutoSize = true, ColumnCount = 2, Dock = DockStyle.Fill };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 56));
        var slider = new TrackBar { Minimum = (int)(min * 100), Maximum = (int)(max * 100), Value = (int)(Math.Max(min, Math.Min(max, value)) * 100), TickFrequency = 25, SmallChange = 5, LargeChange = 25, Dock = DockStyle.Fill, AutoSize = false, Height = 35 };
        var label = new Label { AutoSize = true, Padding = new Padding(0, 7, 0, 0), Text = slider.Value + "%" };
        slider.ValueChanged += delegate { label.Text = slider.Value + "%"; };
        panel.Controls.Add(slider, 0, 0); panel.Controls.Add(label, 1, 0); Row(rows, title, panel);
        writers.Add(x => x.Set(Client.Prefs, key, (slider.Value / 100m).ToString(CultureInfo.InvariantCulture)));
    }
    ComboBox Choice(TableLayoutPanel rows, Ini ini, string key, string title, string[] choices, int fallback)
    {
        var box = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList }; box.Items.AddRange(choices);
        int n; if (!Int32.TryParse(ini.Get(Client.Prefs, key, fallback.ToString()), out n) || n < 0 || n >= choices.Length) n = fallback;
        box.SelectedIndex = n; Row(rows, title, box); writers.Add(x => x.Set(Client.Prefs, key, box.SelectedIndex.ToString())); return box;
    }
    void StopLocation()
    {
        if (locationTimeout != null) { locationTimeout.Stop(); locationTimeout.Dispose(); locationTimeout = null; }
        var watcher = locationWatcher; locationWatcher = null;
        if (watcher != null) { watcher.Stop(); watcher.Dispose(); }
        if (locateButton != null && !locateButton.IsDisposed) locateButton.Enabled = true;
    }
    void LocationUi(GeoCoordinateWatcher watcher, Action action)
    {
        if (IsDisposed || Disposing || !IsHandleCreated) return;
        try { BeginInvoke(new Action(delegate { if (!IsDisposed && !Disposing && locationWatcher == watcher) action(); })); }
        catch (InvalidOperationException) { } // Window closed while Windows supplied an update.
    }
    void FindLocation(NumericUpDown latitude, NumericUpDown longitude, CheckBox enabled, ComboBox source)
    {
        if (preview) return;
        StopLocation();
        try
        {
            locateButton.Enabled = false;
            status.Text = "Waiting for Windows location access. Your saved location is unchanged.";
            var watcher = new GeoCoordinateWatcher(GeoPositionAccuracy.Default);
            locationWatcher = watcher;
            watcher.PositionChanged += delegate(object sender, GeoPositionChangedEventArgs<GeoCoordinate> args)
            {
                var position = args.Position.Location;
                LocationUi(watcher, delegate
                {
                    if (position.IsUnknown || Double.IsNaN(position.Latitude) || Double.IsNaN(position.Longitude) ||
                        Double.IsInfinity(position.Latitude) || Double.IsInfinity(position.Longitude) ||
                        Math.Abs(position.Latitude) > 90 || Math.Abs(position.Longitude) > 180) return;
                    // Weather needs an area, not the most precise device coordinates.
                    latitude.Value = (decimal)Math.Round(position.Latitude, 2);
                    longitude.Value = (decimal)Math.Round(position.Longitude, 2);
                    enabled.Checked = true; source.SelectedIndex = 1;
                    StopLocation();
                    status.Text = "Windows location found. Press Save to use local weather at the next game launch.";
                });
            };
            watcher.StatusChanged += delegate(object sender, GeoPositionStatusChangedEventArgs args)
            {
                LocationUi(watcher, delegate
                {
                    if (watcher.Permission == GeoPositionPermission.Denied || args.Status == GeoPositionStatus.Disabled)
                    {
                        StopLocation();
                        status.Text = "Location is disabled or denied. Open Windows location permissions, or enter coordinates manually.";
                    }
                });
            };
            locationTimeout = new System.Windows.Forms.Timer { Interval = 30000 };
            locationTimeout.Tick += delegate { StopLocation(); status.Text = "Windows could not provide a location. Saved coordinates are unchanged; try again or enter them manually."; };
            locationTimeout.Start();
            watcher.Start(false); // Do not suppress Windows permission UI; never bypass a denial.
        }
        catch (Exception ex) { StopLocation(); status.Text = "Windows location unavailable: " + ex.Message; }
    }
    internal void SelectPreviewTab(string name)
    {
        if (!preview) return;
        foreach (var tabs in Controls.OfType<TableLayoutPanel>().SelectMany(x => x.Controls.OfType<TabControl>()))
            foreach (TabPage page in tabs.TabPages) if (page.Text == name) tabs.SelectedTab = page;
    }
    void Save(bool advanced)
    {
        try
        {
            if (!TryAcquireGate()) { status.Text = "The client is still running (open from this folder: " + DistProcesses() + "). Close it; Save retries automatically."; return; }
            if (rawEdited && !advanced) throw new InvalidOperationException("The INI text was edited. Save from the All settings tab, or reopen Settings before using the other tabs.");
            var ini = advanced ? new Ini(raw.Text) : Ini.Load(Client.SettingsPath);
            if (!advanced)
            {
                string[] parts = resolution.Text.ToLowerInvariant().Split('x'); int w, h;
                if (parts.Length != 2 || !Int32.TryParse(parts[0].Trim(), out w) || !Int32.TryParse(parts[1].Trim(), out h)) throw new InvalidOperationException("Enter a resolution such as 1920 x 1080.");
                if (mode.SelectedIndex == 1) { w = Screen.PrimaryScreen.Bounds.Width; h = Screen.PrimaryScreen.Bounds.Height; }
                Client.SetDisplay(ini, w, h, mode.SelectedIndex);
                ini.Set(Client.Prefs, "DataSource", Client.DataFolder(data.Text));
                foreach (var write in writers) write(ini);
                if (saveProfiles != null) saveProfiles();
            }
            else
            {
                int w, h, m;
                if (!Int32.TryParse(ini.Get(Client.Video,"ResolutionSizeX",""),out w) || !Int32.TryParse(ini.Get(Client.Video,"ResolutionSizeY",""),out h) || !Int32.TryParse(ini.Get(Client.Video,"FullscreenMode",""),out m)) throw new InvalidOperationException("INI must contain a valid display resolution/mode.");
                Client.SetDisplay(ini,w,h,m);
                Client.DataFolder(ini.Get(Client.Prefs,"DataSource",""));
            }
            ini.Save(Client.SettingsPath);
            lastSavedText = String.Join("\r\n", ini.Lines);
            // Reload every control from disk; avoids stale tabs overwriting raw edits.
            MessageBox.Show("Saved. The client will use these settings at its next launch.", "WowUnreal Settings", MessageBoxButtons.OK, MessageBoxIcon.Information);
            Close();
        }
        catch (Exception ex) { status.Text = ex.Message; }
    }
}
