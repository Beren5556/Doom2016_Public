using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: AssemblyTitle("DOOM2016VRDLSS")]
[assembly: AssemblyProduct("DOOM2016VRDLSS")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8", FrameworkDisplayName = ".NET Framework 4.8")]

namespace Doom2016VrDlss
{
    public sealed class PackageManifest
    {
        public string Version { get; set; }
        public string GameSha256 { get; set; }
        public string EngineSha256 { get; set; }
        public Dictionary<string, string> Files { get; set; }
    }

    public sealed class InstallLocations
    {
        public string Runtime, Settings, Desktop;
        public static InstallLocations CurrentUser()
        {
            string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            return new InstallLocations {
                Runtime = Path.Combine(local, "DOOM2016VRDLSS"),
                Settings = Path.Combine(local, "KHARVOX", "settings.json"),
                Desktop = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Doom")
            };
        }
    }

    public static class InstallerEngine
    {
        public static void ConfigurePaths()
        {
            AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling", false);
            AppContext.SetSwitch("Switch.System.IO.BlockLongPaths", false);
        }
        public static readonly JavaScriptSerializer Json = new JavaScriptSerializer();
        public static readonly Encoding Utf8 = new UTF8Encoding(false);
        public static PackageManifest Manifest()
        {
            using (var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("package.json"))
            using (var r = new StreamReader(s, Utf8)) return Json.Deserialize<PackageManifest>(r.ReadToEnd());
        }
        public static string Hash(string file)
        {
            using (var s = File.OpenRead(file)) using (var h = SHA256.Create())
                return BitConverter.ToString(h.ComputeHash(s)).Replace("-", "").ToLowerInvariant();
        }
        public static string GameDirectory(string directory, PackageManifest manifest)
        {
            string root = Path.GetFullPath(directory.Trim().Trim('"'));
            string exe = Path.Combine(root, "DOOMx64vk.exe");
            if (!File.Exists(exe) || !Directory.Exists(Path.Combine(root, "base")) ||
                !File.Exists(Path.Combine(root, "steam_api64.dll")))
                throw new InvalidOperationException("Selecciona la carpeta de DOOM (2016) que contiene DOOMx64vk.exe y la carpeta base.");
            if (!String.Equals(Hash(exe), manifest.GameSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Esta versión de DOOM no coincide con la versión compatible verificada. No se ha instalado nada.");
            string[] loaders = { "dinput8.dll", "dxgi.dll", "d3d11.dll", "d3d12.dll", "opengl32.dll", "version.dll", "winmm.dll", "winhttp.dll", "dsound.dll", "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll", "RealVR64.dll", "RealVR.dll", "RealVR.ini", "RealConfig.bat" };
            var conflicts = Directory.GetFiles(root).Where(p => loaders.Contains(Path.GetFileName(p), StringComparer.OrdinalIgnoreCase) || p.EndsWith(".asi", StringComparison.OrdinalIgnoreCase)).Select(Path.GetFileName).ToList();
            if (Directory.Exists(Path.Combine(root, "RealRepo"))) conflicts.Add("RealRepo");
            if (conflicts.Count != 0) throw new InvalidOperationException("Hay otros cargadores de mods en la carpeta de DOOM: " + String.Join(", ", conflicts) + ". Retíralos antes de instalar.");
            return root;
        }
        public static string Child(string root, string relative)
        {
            if (String.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) || relative.IndexOf(':') >= 0 || relative.Contains("\\") || relative.Split('/').Any(p => p == ".." || p == "." || p.Length == 0 || p.EndsWith(" ") || p.EndsWith(".")))
                throw new InvalidDataException("Ruta no válida en el paquete: " + relative);
            string full = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
            if (!full.StartsWith(Path.GetFullPath(root).TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Ruta fuera del destino.");
            return full;
        }
        public static void NoRedirects(string path)
        {
            for (string p = Path.GetFullPath(path); !String.IsNullOrEmpty(p); p = Path.GetDirectoryName(p))
                if ((Directory.Exists(p) || File.Exists(p)) && (File.GetAttributes(p) & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("El destino contiene un enlace o una carpeta redirigida: " + p);
        }
        public static void Extract(Stream source, string destination, PackageManifest manifest, Action<int, string> progress)
        {
            NoRedirects(destination);
            using (var archive = new ZipArchive(source, ZipArchiveMode.Read, true))
            {
                var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                int done = 0;
                foreach (var entry in archive.Entries)
                {
                    string expected;
                    if (!seen.Add(entry.FullName) || !manifest.Files.TryGetValue(entry.FullName, out expected)) throw new InvalidDataException("Archivo inesperado o duplicado: " + entry.FullName);
                    string file = Child(destination, entry.FullName);
                    NoRedirects(file);
                    Directory.CreateDirectory(Path.GetDirectoryName(file));
                    using (var input = entry.Open()) using (var output = new FileStream(file, FileMode.CreateNew, FileAccess.Write)) input.CopyTo(output);
                    if (!String.Equals(Hash(file), expected, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("El paquete está dañado: " + entry.FullName);
                    if (progress != null && (++done % 30 == 0 || done == manifest.Files.Count)) progress(5 + done * 75 / manifest.Files.Count, "Instalando componentes…");
                }
                if (seen.Count != manifest.Files.Count) throw new InvalidDataException("El paquete está incompleto.");
            }
        }
        public static Dictionary<string, object> Settings(string existing, string template, string game)
        {
            var defaults = Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(template, Utf8));
            var value = File.Exists(existing) ? Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(existing, Utf8)) : new Dictionary<string, object>();
            if (value == null) throw new InvalidDataException("La configuración existente de KHARVOX no es válida.");
            foreach (var p in defaults) if (!value.ContainsKey(p.Key)) value[p.Key] = p.Value;
            value["GamePath"] = game; value["RendererMode"] = "VULKAN_SFS";
            value["UseFsrUpscaling"] = false; value["DisableAa"] = false;
            return value;
        }
        public static void AtomicText(string path, string text)
        {
            NoRedirects(path); Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try {
                File.WriteAllText(temp, text, Utf8);
                if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path);
            } finally { if (File.Exists(temp)) File.Delete(temp); }
        }
        public static void CheckDependencies(string root)
        {
            Run(Path.Combine(root, "runtimes", "python", "python.exe"), "-B -c \"import sys,PIL,ssl,ctypes;from PIL import Image;assert sys.flags.isolated;assert Image.new('RGBA',(2,2)).tobytes()\"", root, null);
            Run(Path.Combine(root, "runtimes", "node", "node.exe"), "-e \"require('playwright');require('playwright-core');\"", root, Path.Combine(root, "runtimes", "node", "node_modules"));
        }
        static void Run(string exe, string args, string cwd, string modules)
        {
            var psi = new ProcessStartInfo(exe, args) { WorkingDirectory = cwd, UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
            psi.EnvironmentVariables.Remove("PYTHONHOME"); psi.EnvironmentVariables.Remove("PYTHONPATH");
            if (modules != null) psi.EnvironmentVariables["NODE_PATH"] = modules;
            using (var p = Process.Start(psi)) {
                var stdout = p.StandardOutput.ReadToEndAsync(); var stderr = p.StandardError.ReadToEndAsync();
                if (!p.WaitForExit(30000)) { p.Kill(); throw new IOException("La comprobación de componentes ha agotado el tiempo."); }
                Task.WaitAll(stdout, stderr);
                if (p.ExitCode != 0) throw new IOException("No se pudo iniciar " + Path.GetFileName(exe) + ": " + stderr.Result);
            }
        }
        static void Shortcut(string path, string target, string cwd)
        {
            NoRedirects(path); Directory.CreateDirectory(Path.GetDirectoryName(path));
            Type type = Type.GetTypeFromProgID("WScript.Shell");
            object shell = Activator.CreateInstance(type);
            object link = type.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
            Type lt = link.GetType();
            lt.InvokeMember("TargetPath", BindingFlags.SetProperty, null, link, new object[] { target });
            lt.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, link, new object[] { cwd });
            lt.InvokeMember("Description", BindingFlags.SetProperty, null, link, new object[] { "DOOM VR — DLAA, DLSS y foveado" });
            lt.InvokeMember("Save", BindingFlags.InvokeMethod, null, link, null);
            System.Runtime.InteropServices.Marshal.FinalReleaseComObject(link);
            System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
        }
        public static string Install(string game, InstallLocations locations, Action<int, string> progress)
        {
            var manifest = Manifest(); game = GameDirectory(game, manifest);
            foreach (string name in new[] { "DOOMx64vk", "DOOMx64", "KharvoxLauncher", "DoomVR" })
                if (Process.GetProcessesByName(name).Length > 0) throw new IOException("Cierra DOOM y su lanzador antes de instalar.");
            string edge = Launcher.FindEdge();
            if (edge == null) throw new IOException("El panel necesita Microsoft Edge. Instálalo y vuelve a abrir este instalador.");
            string target = Path.GetFullPath(locations.Runtime).TrimEnd('\\');
            if (Path.GetDirectoryName(target) == null || target == game || game.StartsWith(target + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("Destino de instalación no válido.");
            NoRedirects(target); NoRedirects(locations.Settings); NoRedirects(locations.Desktop);
            if (Directory.Exists(target)) {
                string marker = Path.Combine(target, "installation.json");
                if (!File.Exists(marker) || !Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(marker, Utf8)).ContainsKey("DOOM2016VRDLSS"))
                    throw new IOException("El destino ya existe y no pertenece a DOOM2016VRDLSS.");
            }
            string token = Guid.NewGuid().ToString("N");
            string stage = target + ".stage-" + token, previous = target + ".previous-" + token;
            string shortcut = Path.Combine(locations.Desktop, "DOOM VR.lnk");
            byte[] settingsBefore = File.Exists(locations.Settings) ? File.ReadAllBytes(locations.Settings) : null;
            byte[] linkBefore = File.Exists(shortcut) ? File.ReadAllBytes(shortcut) : null;
            bool promoted = false, retired = false, settingsChanged = false, shortcutChanged = false;
            try {
                Directory.CreateDirectory(stage);
                if (progress != null) progress(3, "Verificando el paquete…");
                using (var zip = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip")) Extract(zip, stage, manifest, progress);
                if (Hash(Path.Combine(stage, "KharvoxLayer.dll")) != manifest.EngineSha256) throw new InvalidDataException("La DLL no coincide con la versión validada.");
                string neuralConfig = Path.Combine(target, "DOOMNeuralPost.ini");
                NoRedirects(neuralConfig);
                if (File.Exists(neuralConfig)) File.Copy(neuralConfig, Path.Combine(stage, "DOOMNeuralPost.ini"), true);
                if (Directory.Exists(target)) foreach (string name in new[] { "startup.cfg", "foveation-profiles.json" }) {
                    string old = Path.Combine(target, "DoomLayer", name);
                    NoRedirects(old);
                    if (File.Exists(old)) File.Copy(old, Path.Combine(stage, "DoomLayer", name), true);
                }
                string own = Path.Combine(stage, "DoomLayer"), finalOwn = Path.Combine(target, "DoomLayer");
                File.WriteAllText(Path.Combine(own, "python.cfg"), Path.Combine(target, "runtimes", "python", "python.exe"), Utf8);
                File.WriteAllText(Path.Combine(own, "runtime.json"), Json.Serialize(new { node = Path.Combine(target, "runtimes", "node", "node.exe"), modules = Path.Combine(target, "runtimes", "node", "node_modules") }), Utf8);
                var layer = Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(stage, "KharvoxLayer.json"), Utf8));
                ((Dictionary<string, object>)layer["layer"])["library_path"] = Path.Combine(target, "KharvoxLayer.dll");
                File.WriteAllText(Path.Combine(stage, "KharvoxLayer.json"), Json.Serialize(layer), Utf8);
                var inventory = Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(own, "inventory.json"), Utf8));
                var files = (Dictionary<string, object>)inventory["files"];
                foreach (string n in new[] { "python.cfg", "runtime.json" }) files[n] = Hash(Path.Combine(own, n));
                File.WriteAllText(Path.Combine(own, "inventory.json"), Json.Serialize(inventory), Utf8);
                Directory.CreateDirectory(Path.Combine(own, "cache")); Directory.CreateDirectory(Path.Combine(own, "logs"));
                File.WriteAllText(Path.Combine(stage, "installation.json"), Json.Serialize(new Dictionary<string, object> { { "DOOM2016VRDLSS", manifest.Version }, { "gameDirectory", game }, { "gameSha256", manifest.GameSha256 }, { "engineSha256", manifest.EngineSha256 }, { "installedUtc", DateTime.UtcNow.ToString("o") } }), Utf8);
                var settings = Settings(locations.Settings, Path.Combine(stage, "installer-settings.json"), game);
                if (progress != null) progress(85, "Comprobando las dependencias…");
                CheckDependencies(stage);
                if (Directory.Exists(target)) { Directory.Move(target, previous); retired = true; }
                Directory.Move(stage, target); promoted = true;
                AtomicText(locations.Settings, Json.Serialize(settings)); settingsChanged = true;
                shortcutChanged = true; Shortcut(shortcut, Path.Combine(target, "DoomVR.exe"), target);
                if (progress != null) progress(100, "Instalación completada. Acceso: escritorio → Doom → DOOM VR.");
                // Keep the previous owned runtime for recovery; no game files are replaced.
                return target;
            } catch {
                if (shortcutChanged) { if (linkBefore == null) { if (File.Exists(shortcut)) File.Delete(shortcut); } else File.WriteAllBytes(shortcut, linkBefore); }
                if (settingsChanged) { if (settingsBefore == null) { if (File.Exists(locations.Settings)) File.Delete(locations.Settings); } else File.WriteAllBytes(locations.Settings, settingsBefore); }
                if (promoted) Directory.Move(target, stage);
                if (retired) Directory.Move(previous, target);
                throw;
            } finally {
                // This is always the checked, uniquely generated staging directory.
                if (Directory.Exists(stage)) { NoRedirects(stage); Directory.Delete(stage, true); }
            }
        }
    }

    public static class Launcher
    {
        public static string FindEdge()
        {
            foreach (string root in new[] { Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData) }) {
                string p = Path.Combine(root, "Microsoft", "Edge", "Application", "msedge.exe"); if (File.Exists(p)) return p;
            }
            return null;
        }
        public static void Run()
        {
            string root = AppDomain.CurrentDomain.BaseDirectory;
            var installed = InstallerEngine.Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(root, "installation.json"), InstallerEngine.Utf8));
            string game = (string)installed["gameDirectory"];
            if (!File.Exists(Path.Combine(game, "DOOMx64vk.exe"))) throw new IOException("La carpeta de DOOM ha cambiado. Abre el instalador y selecciona su ubicación actual.");
            var locations = InstallLocations.CurrentUser();
            InstallerEngine.AtomicText(locations.Settings, InstallerEngine.Json.Serialize(InstallerEngine.Settings(locations.Settings, Path.Combine(root, "installer-settings.json"), game)));
            if (Process.GetProcessesByName("steam").Length == 0) {
                using (var key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam")) {
                    string steam = key == null ? null : key.GetValue("SteamExe") as string;
                    if (steam == null || !File.Exists(steam)) throw new IOException("Inicia Steam y vuelve a abrir DOOM VR.");
                    Process.Start(new ProcessStartInfo(steam, "-silent") { UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden });
                }
                System.Threading.Thread.Sleep(5000);
            }
            var psi = new ProcessStartInfo(Path.Combine(root, "KharvoxLauncher.exe"), "-run") { WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true };
            foreach (string n in new[] { "VK_ADD_IMPLICIT_LAYER_PATH", "VK_LOADER_LAYERS_ENABLE", "ENABLE_VULKAN_RENDERDOC_CAPTURE", "KHARVOX_RENDERDOC_CAPTURE", "KHARVOX_TEMPORAL_AUDIT_PATH", "KHARVOX_TEMPORAL_CONTEXT", "KHARVOX_GEOMETRY_AUDIT" }) psi.EnvironmentVariables.Remove(n);
            psi.EnvironmentVariables["DISABLE_VULKAN_RENDERDOC_CAPTURE_1_46"] = "1";
            foreach (string n in new[] { "KHARVOX_TEMPORAL_BRIDGE", "KHARVOX_TEMPORAL_PARAMETERS", "KHARVOX_DLSS_DLAA", "KHARVOX_DLSS_RAW_HDR", "KHARVOX_DLSS_PREPARE" }) psi.EnvironmentVariables[n] = "1";
            psi.EnvironmentVariables["KHARVOX_DLSS_RUNTIME"] = Path.Combine(root, "DoomLayer", "ngx");
            psi.EnvironmentVariables["KHARVOX_DLSS_CACHE"] = Path.Combine(root, "DoomLayer", "cache");
            using (var process = Process.Start(psi)) {
                process.WaitForExit();
                if (process.ExitCode != 0) {
                    string log = Path.Combine(Path.GetTempPath(), "KHARVOX-launcher-run.log");
                    string detail = File.Exists(log) ? String.Join(Environment.NewLine, File.ReadAllLines(log).Reverse().Take(12).Reverse()) : "Consulta los registros de KHARVOX.";
                    throw new IOException("No se pudo iniciar DOOM VR." + Environment.NewLine + detail);
                }
            }
        }
    }

    public sealed class InstallerForm : Form
    {
        readonly TextBox folder = new TextBox();
        readonly Button browse = new Button(), install = new Button();
        readonly Label status = new Label();
        readonly ProgressBar progress = new ProgressBar();
        bool installing, completed;
        public InstallerForm()
        {
            Text = "DOOM2016VRDLSS"; ClientSize = new Size(580, 245); FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false; StartPosition = FormStartPosition.CenterScreen; Font = new Font("Segoe UI", 10);
            var title = new Label { Text = "Instalar DOOM VR", Font = new Font("Segoe UI", 18, FontStyle.Bold), AutoSize = true, Location = new Point(22, 17) };
            var label = new Label { Text = "Carpeta de instalación de DOOM (2016)", AutoSize = true, Location = new Point(24, 62) };
            folder.SetBounds(24, 90, 418, 29); browse.Text = "Examinar…"; browse.SetBounds(451, 88, 105, 31);
            install.Text = "Instalar"; install.SetBounds(436, 193, 120, 33);
            progress.SetBounds(24, 133, 532, 15); status.SetBounds(24, 155, 532, 36);
            status.Text = "DLAA · DLSS · Foveado · Layer";
            Controls.AddRange(new Control[] { title, label, folder, browse, progress, status, install });
            AcceptButton = install;
            browse.Click += delegate { using (var dialog = new FolderBrowserDialog { Description = "Selecciona la carpeta que contiene DOOMx64vk.exe", ShowNewFolderButton = false }) { if (dialog.ShowDialog(this) == DialogResult.OK) folder.Text = dialog.SelectedPath; } };
            install.Click += async delegate {
                if (completed) { Close(); return; }
                if (String.IsNullOrWhiteSpace(folder.Text)) { status.Text = "Selecciona primero la carpeta de DOOM."; return; }
                string selected = folder.Text; installing = true; browse.Enabled = install.Enabled = folder.Enabled = false;
                try {
                    status.Text = "Comprobando compatibilidad…";
                    var updates = new Progress<Tuple<int, string>>(u => { progress.Value = u.Item1; status.Text = u.Item2; });
                    await Task.Run(() => InstallerEngine.Install(selected, InstallLocations.CurrentUser(), (n, t) => ((IProgress<Tuple<int, string>>)updates).Report(Tuple.Create(n, t))));
                    install.Text = "Cerrar";
                    completed = true;
                    install.Enabled = true;
                    folder.ReadOnly = true;
                } catch (Exception e) { status.Text = "No se pudo completar la instalación."; MessageBox.Show(this, e.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error); browse.Enabled = install.Enabled = folder.Enabled = true; }
                finally { installing = false; }
            };
            FormClosing += delegate(object sender, FormClosingEventArgs e) { if (installing) e.Cancel = true; };
        }
    }

    public static class Program
    {
        [STAThread] public static int Main(string[] args)
        {
            InstallerEngine.ConfigurePaths();
            Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
            try {
#if LAUNCHER
                Launcher.Run();
#else
                Application.Run(new InstallerForm());
#endif
                return 0;
            } catch (Exception e) { MessageBox.Show(e.Message, "DOOM2016VRDLSS", MessageBoxButtons.OK, MessageBoxIcon.Error); return 1; }
        }
    }
}
