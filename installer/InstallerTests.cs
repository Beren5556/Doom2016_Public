using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using Doom2016VrDlss;
[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8")]

static class InstallerTests
{
    static int assertions;
    static void Check(bool condition, string message) { if (!condition) throw new Exception(message); ++assertions; }
    static void Rejected(Action action, string message) {
        bool failed = false; try { action(); } catch { failed = true; }
        Check(failed, message);
    }
    static string Sha(byte[] bytes) {
        using (var hash = System.Security.Cryptography.SHA256.Create()) return BitConverter.ToString(hash.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
    }
    static MemoryStream Zip(params string[] names) {
        var stream = new MemoryStream();
        using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, true))
            foreach (string name in names) using (var writer = new StreamWriter(archive.CreateEntry(name).Open())) writer.Write("actual");
        stream.Position = 0; return stream;
    }
    [STAThread] static int Main(string[] args)
    {
        InstallerEngine.ConfigurePaths();
        if (args.Length == 2 && args[0] == "--preview") {
            System.Windows.Forms.Application.EnableVisualStyles();
            using (var form = new InstallerForm()) using (var image = new System.Drawing.Bitmap(form.Width, form.Height)) {
                form.StartPosition = System.Windows.Forms.FormStartPosition.Manual;
                form.Location = new System.Drawing.Point(-32000, -32000);
                form.Show(); System.Windows.Forms.Application.DoEvents();
                form.DrawToBitmap(image, new System.Drawing.Rectangle(0, 0, image.Width, image.Height));
                image.Save(args[1], System.Drawing.Imaging.ImageFormat.Png);
                form.Close();
            }
            return 0;
        }
        string root = Path.GetFullPath(args[1]); Directory.CreateDirectory(root);
        var manifest = InstallerEngine.Manifest();
        string expectedEngine = args.Length > 2 ? args[2] : "8a72c744b83dea42df0482e951b99a2f38c74c14c833b0ff346d3942dbcf992a";
        Check(manifest.EngineSha256 == expectedEngine, "accepted engine");
        Rejected(() => InstallerEngine.GameDirectory(Path.Combine(root, "missing"), manifest), "missing game rejected");
        foreach (string bad in new[] { "../escape", "/absolute", "C:/absolute", "x/../../escape", "a\\b", "x:y", "a./b" })
            Rejected(() => InstallerEngine.Child(root, bad), "unsafe path rejected: " + bad);
        string invalid = Path.Combine(root, "invalid"); Directory.CreateDirectory(Path.Combine(invalid, "base"));
        File.WriteAllBytes(Path.Combine(invalid, "DOOMx64vk.exe"), new byte[128]); File.WriteAllBytes(Path.Combine(invalid, "steam_api64.dll"), new byte[1]);
        Rejected(() => InstallerEngine.GameDirectory(invalid, manifest), "incompatible game rejected");
        var small = new PackageManifest { Files = new Dictionary<string, string> { { "data.txt", Sha(Encoding.UTF8.GetBytes("actual")) } } };
        using (var zip = Zip("../escape")) Rejected(() => InstallerEngine.Extract(zip, Path.Combine(root, "traversal"), small, null), "unknown/traversal archive rejected");
        using (var zip = Zip("data.txt", "data.txt")) Rejected(() => InstallerEngine.Extract(zip, Path.Combine(root, "duplicate"), small, null), "duplicate archive rejected");
        small.Files["data.txt"] = new string('0', 64);
        using (var zip = Zip("data.txt")) Rejected(() => InstallerEngine.Extract(zip, Path.Combine(root, "corrupt"), small, null), "corrupt archive rejected");
        string game = Path.Combine(root, "Juego de prueba"); Directory.CreateDirectory(Path.Combine(game, "base"));
        File.Copy(Path.Combine(args[0], "DOOMx64vk.exe"), Path.Combine(game, "DOOMx64vk.exe"));
        File.Copy(Path.Combine(args[0], "steam_api64.dll"), Path.Combine(game, "steam_api64.dll"));
        File.WriteAllBytes(Path.Combine(game, "dxgi.dll"), new byte[1]);
        Rejected(() => InstallerEngine.GameDirectory(game, manifest), "conflicting mod rejected");
        File.Delete(Path.Combine(game, "dxgi.dll"));
        Check(InstallerEngine.GameDirectory(game, manifest) == game, "compatible game accepted");
        var locations = new InstallLocations {
            Runtime = Path.Combine(root, "Instalación con espacios", "DOOM2016VRDLSS"),
            Settings = Path.Combine(root, "perfil", "settings.json"),
            Desktop = Path.Combine(root, "escritorio", "Doom")
        };
        Directory.CreateDirectory(Path.GetDirectoryName(locations.Settings));
        File.WriteAllText(locations.Settings, "{\"SmoothSpeed\":123,\"GamePath\":\"old\",\"RendererMode\":\"AER\"}");
        string result = System.Threading.Tasks.Task.Run(() => InstallerEngine.Install(game, locations, (n, text) => { if (n >= 80) Console.WriteLine(n + " " + text); })).GetAwaiter().GetResult();
        Check(result == locations.Runtime, "isolated installation completed");
        Check(InstallerEngine.Hash(Path.Combine(result, "KharvoxLayer.dll")) == manifest.EngineSha256, "engine remains byte-identical");
        Check(Directory.GetFiles(Path.Combine(result, "sfs-profile"), "*.spv").Length == 75, "75 shader profiles");
        Check(File.Exists(Path.Combine(locations.Desktop, "DOOM VR.lnk")), "shortcut created");
        string own = Path.Combine(result, "DoomLayer");
        Check(File.ReadAllText(Path.Combine(own, "python.cfg")).StartsWith(result), "Python path relocated");
        Check(!File.ReadAllText(Path.Combine(own, "runtime.json")).Contains(".cache"), "Node independent from Codex");
        Check(Directory.GetFiles(own, "request-*").Length == 0 && Directory.GetFiles(own, "status-*").Length == 0, "old process state excluded");
        var settings = InstallerEngine.Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(locations.Settings));
        Check((int)settings["SmoothSpeed"] == 123, "existing controls preserved");
        Check((string)settings["GamePath"] == game && (string)settings["RendererMode"] == "VULKAN_SFS", "game and renderer configured");
        var inventory = InstallerEngine.Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(own, "inventory.json")));
        Check((bool)inventory["neuralEnabled"] == false, "Neural remains disabled");
        foreach (var pair in (Dictionary<string, object>)inventory["files"])
            Check(InstallerEngine.Hash(Path.Combine(own, pair.Key.Replace('/', '\\'))) == (string)pair.Value, "installed overlay file verified: " + pair.Key);
        string startup = File.ReadAllText(Path.Combine(own, "startup.cfg"));
        string neuralConfig = Path.Combine(result, "DOOMNeuralPost.ini");
        string userNeural = File.Exists(neuralConfig) ? File.ReadAllText(neuralConfig) + "\n; Preserved user configuration\n" : null;
        if (userNeural != null) File.WriteAllText(neuralConfig, userNeural);
        File.AppendAllText(Path.Combine(own, "startup.cfg"), "\n");
        string userStartup = File.ReadAllText(Path.Combine(own, "startup.cfg"));
        InstallerEngine.Install(game, locations, null);
        Check(File.ReadAllText(Path.Combine(own, "startup.cfg")) == userStartup, "user Layer choices preserved on reinstall");
        if (userNeural != null) Check(File.ReadAllText(neuralConfig) == userNeural, "user Neural choices preserved on reinstall");
        byte[] before = File.ReadAllBytes(locations.Settings);
        string blocker = Path.Combine(root, "blocked-desktop"); File.WriteAllText(blocker, "blocked");
        var failing = new InstallLocations { Runtime = locations.Runtime, Settings = locations.Settings, Desktop = blocker };
        Rejected(() => InstallerEngine.Install(game, failing, null), "shortcut failure rolls installation back");
        Check(before.SequenceEqual(File.ReadAllBytes(locations.Settings)), "settings restored on rollback");
        Check(File.ReadAllText(Path.Combine(own, "startup.cfg")) == userStartup, "previous runtime restored on rollback");
        Check(InstallerEngine.Hash(Path.Combine(result, "KharvoxLayer.dll")) == manifest.EngineSha256, "rollback engine identity");
        Check(!Directory.GetDirectories(Path.GetDirectoryName(result), "*.stage-*").Any(), "staging cleaned after rollback");
        Check(InstallerEngine.Hash(Path.Combine(game, "DOOMx64vk.exe")) == manifest.GameSha256, "game executable unchanged");
        Console.WriteLine("PASS " + assertions + " assertions; no game/VR started.");
        Console.WriteLine("TEST_RUNTIME=" + result);
        return 0;
    }
}
