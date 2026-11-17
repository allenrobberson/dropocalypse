// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

static double nowSeconds();

/// ~/Videos (or the folder the desktop calls "Videos" in the user's language).
static std::wstring defaultRecordFolder() {
    std::string out;
    if (runProgram({"xdg-user-dir", "VIDEOS"}, &out) == 0) {
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        if (!out.empty() && out != homeDir()) return widen(out);
    }
    return widen(homeDir() + "/Videos");
}

static std::wstring recordFolder() {
    static std::wstring cached;
    if (!g.settings.recordFolder.empty()) return g.settings.recordFolder;
    if (cached.empty()) cached = defaultRecordFolder();
    return cached;
}

