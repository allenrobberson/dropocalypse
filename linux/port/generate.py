#!/usr/bin/env python3
"""Builds linux/src/main.cpp from windows/src/main.cpp and the Linux fragments in this folder.

The toolbar, panels, settings file and update check are shared with the Windows version, so they
are copied from windows/src/main.cpp section by section. Everything that touches the operating
system (windows, OpenGL, files, dialogs, the updater, the main loop) comes from the *.inc files
here. After changing the Windows UI, run:

    python3 linux/port/generate.py

and commit the regenerated linux/src/main.cpp. The script fails loudly if a section it relies on
has moved or been renamed, so a broken sync shows up right away.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PORT = ROOT / "linux" / "port"
WIN = (ROOT / "windows" / "src" / "main.cpp").read_text().split("\n")


def find(marker, start=0):
    for i in range(start, len(WIN)):
        if WIN[i].startswith(marker):
            return i
    sys.exit(f"generate.py: marker not found in windows/src/main.cpp: {marker!r}")


def section(start_marker, end_marker, keep_doc=True):
    """Lines from start_marker up to (not including) end_marker, without the trailing comments
    and blank lines that belong to the next section."""
    a = find(start_marker)
    b = find(end_marker, a + 1)
    if keep_doc:
        while a > 0 and WIN[a - 1].startswith("///"):
            a -= 1
    lines = WIN[a:b]
    while lines and (not lines[-1].strip() or lines[-1].startswith("//")):
        lines.pop()
    return "\n".join(lines) + "\n\n"


def inc(name):
    return (PORT / name).read_text().rstrip("\n") + "\n\n"


def replace(text, old, new, count=1):
    if old not in text:
        sys.exit(f"generate.py: expected text not found: {old[:70]!r}")
    return text.replace(old, new, count)


out = []
out.append(inc("head.inc"))
out.append(section("struct AppSettings {", "static std::wstring appDataDir()"))
out.append(inc("appdata.inc"))
settings = section("static std::string serialize", "static void writeFile")
settings = replace(settings, '''    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;''', '''    FILE* f = std::fopen(narrow(path).c_str(), "rb");
    if (!f) return false;''')
out.append(settings)
out.append(inc("writefile.inc"))
out.append(section("static void resetMotion", "struct App {"))
out.append(inc("app.inc"))
out.append("// ---------------------------------------------------------------------------------------------\n")
out.append(section("// Update check:", "static bool httpsGetUrl", keep_doc=False))
out.append(inc("http.inc"))
out.append(section("static std::string jsonString", "static void openDownloadPage"))
out.append(inc("opendownload.inc"))
out.append(inc("install.inc"))
notices = section("static void drawPrivacyNotice", "// Logo handling")
notices = replace(notices, '''        if (ui3d::smallButton("Read the privacy policy"))
            ShellExecuteW(nullptr, L"open", L"https://github.com/allenrobberson/dropocalypse/blob/main/PRIVACY.md",
                          nullptr, nullptr, SW_SHOWNORMAL);''', '''        if (ui3d::smallButton("Read the privacy policy"))
            openWithDesktop("https://github.com/allenrobberson/dropocalypse/blob/main/PRIVACY.md");''')
notices = replace(notices, '''if (ui3d::button("Full changelog")) ShellExecuteW(nullptr, L"open", L"https://github.com/allenrobberson/dropocalypse/wiki/Changelog", nullptr, nullptr, SW_SHOWNORMAL);''',
                  '''if (ui3d::button("Full changelog")) openWithDesktop("https://github.com/allenrobberson/dropocalypse/wiki/Changelog");''')
out.append(notices)
out.append(inc("dialogs.inc"))
out.append("// ---------------------------------------------------------------------------------------------\n"
           "// Logo handling\n"
           "// ---------------------------------------------------------------------------------------------\n\n")
out.append(section("static std::wstring fileNameOf", "static bool copyItemFile"))
out.append(inc("logo.inc"))
out.append(section("static bool isOwnItemFile", "static void chooseLogo"))
out.append(inc("chooselogo.inc"))
out.append(inc("recordfolder.inc"))
out.append(section("static void showRecordNotice", "static void startRecording"))
out.append(inc("startrecording.inc"))
out.append(section("static void stopRecording", "static void showInExplorer"))
out.append(inc("explorer.inc"))
record = section("static std::string recordTimeLabel", "// Window / swap chain")
record = replace(record, '''    ImGui::TextDisabled("Records the visuals and the music''', '''    static int ffmpegKnown = -1;   // checked once: recording uses ffmpeg
    if (ffmpegKnown < 0) ffmpegKnown = Recorder::available() ? 1 : 0;
    if (!ffmpegKnown) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1));
        ImGui::TextWrapped("Recording needs ffmpeg. Install it with your package manager (for example: "
                           "sudo apt install ffmpeg), then reopen Dropocalypse.");
        ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("Records the visuals and the music''')
out.append(record)
out.append(inc("window.inc"))
out.append(section("static void nextScene", "static void rebuildFonts"))
out.append(inc("fonts.inc"))
ui = section("static bool slider", "// Window procedure")
ui = ui.replace("HMONITOR cur = visualsMonitor();", "GLFWmonitor* cur = visualsMonitor();")
ui = ui.replace("System audio (what this PC is playing)", "What this computer is playing")
if re.search(r"\b(HMONITOR|HWND|ShellExecute|g\.hwnd|g\.ctrlHwnd)\b", ui):
    sys.exit("generate.py: Windows-only code found in the shared UI section; add a replacement here.")
out.append(ui)
out.append(inc("tail.inc"))

text = "".join(out).rstrip("\n") + "\n"
header = ("// GENERATED by linux/port/generate.py from windows/src/main.cpp and linux/port/*.inc.\n"
          "// Edit those files and run the script instead of editing this one.\n")
(ROOT / "linux" / "src" / "main.cpp").write_text(header + text)
print(f"wrote linux/src/main.cpp ({text.count(chr(10))} lines)")
