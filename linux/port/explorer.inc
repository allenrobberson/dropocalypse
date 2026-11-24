static void showInExplorer(const std::wstring& path) {
    // Ask the file manager to select the file; fall back to opening its folder.
    std::string p = narrow(path);
    std::string uri = "file://" + p;
    if (runProgram({"dbus-send", "--session", "--dest=org.freedesktop.FileManager1", "--type=method_call",
                    "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1.ShowItems",
                    "array:string:" + uri, "string:"}) == 0)
        return;
    size_t slash = p.find_last_of('/');
    openWithDesktop(slash == std::string::npos ? p : p.substr(0, slash));
}

static void openRecordFolder() {
    std::string dir = narrow(recordFolder());
    makeDirs(dir);
    openWithDesktop(dir);
}

static void chooseRecordFolder() {
    std::string start = narrow(recordFolder()) + "/";
    runDialog(2, {"zenity", "--file-selection", "--directory", "--title=Choose where to save recordings",
                  "--filename=" + start},
              {"kdialog", "--title", "Choose where to save recordings", "--getexistingdirectory", start});
}

/// Picks up a finished file dialog (called once per frame).
static void pollDialog() {
    std::lock_guard<std::mutex> lock(gDialog.m);
    if (!gDialog.ready) return;
    gDialog.ready = false;
    if (gDialog.result == "\x01nodialog") {
        if (gDialog.kind == 1) g.pendingReplace = -1;
        g.errorMessage = gDialog.kind == 1
                             ? L"No file chooser found (zenity or kdialog). Drag an image onto the window instead."
                             : L"No folder chooser found. Install zenity or kdialog to pick a folder.";
        return;
    }
    if (gDialog.result.empty()) {   // cancelled: the next image is added, not swapped in
        if (gDialog.kind == 1) g.pendingReplace = -1;
        return;
    }
    if (gDialog.kind == 1) loadLogoFrom(widen(gDialog.result));
    else if (gDialog.kind == 2) g.settings.recordFolder = widen(gDialog.result);
    else if (gDialog.kind == 3) {
        g.settings.milkFolder = widen(gDialog.result);
        g.milkRescan = true;
    }
}

// ---- MilkDrop presets: where they live on this computer.

/// The bundled presets are unpacked here (see milkdrop_bundled.h).
static std::filesystem::path milkBundledDir() { return std::filesystem::path(narrow(appDataDir())) / "milkdrop"; }
static std::filesystem::path milkUserDir() {
    return g.settings.milkFolder.empty() ? std::filesystem::path() : std::filesystem::path(narrow(g.settings.milkFolder));
}
/// The folder this program runs from (projectM's library sits beside it, or in ../lib).
static std::filesystem::path milkLibraryDir() {
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path(".") : exe.parent_path();
}

static void chooseMilkFolder() {
    std::string start = g.settings.milkFolder.empty() ? homeDir() + "/" : narrow(g.settings.milkFolder) + "/";
    runDialog(3, {"zenity", "--file-selection", "--directory",
                  "--title=Choose the folder with your MilkDrop presets (.milk files)", "--filename=" + start},
              {"kdialog", "--title", "Choose the folder with your MilkDrop presets", "--getexistingdirectory", start});
}

static void openMilkFolder() {
    if (!g.settings.milkFolder.empty()) openWithDesktop(narrow(g.settings.milkFolder));
}

