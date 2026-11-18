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
}

