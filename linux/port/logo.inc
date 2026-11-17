static bool copyFile(const std::string& from, const std::string& to) {
    FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = std::fopen(to.c_str(), "wb");
    if (!out) { std::fclose(in); return false; }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0)
        if (std::fwrite(buf, 1, n, out) != n) { ok = false; break; }
    std::fclose(in);
    ok = std::fclose(out) == 0 && ok;
    return ok;
}

static void loadLogoFrom(const std::wstring& path) {
    LogoImage img;
    std::wstring err;
    if (!loadLogoImage(path, img, err)) {
        g.errorMessage = err;
        return;
    }
    // Keep a private copy so the logo survives the original being moved or deleted.
    std::wstring dest = g.dataDir + L"/logo" + extensionOf(path);
    if (dest != path) {
        if (!g.settings.logoFile.empty() && g.settings.logoFile != dest) std::remove(narrow(g.settings.logoFile).c_str());
        copyFile(narrow(path), narrow(dest));
    }
    g.renderer.setLogo(img);
    g.settings.logoFile = dest;
    g.settings.logoName = fileNameOf(path);
    g.settings.r.showLogo = true;
    g.errorMessage.clear();
}

static void clearLogo() {
    if (!g.settings.logoFile.empty()) std::remove(narrow(g.settings.logoFile).c_str());
    g.settings.logoFile.clear();
    g.settings.logoName.clear();
    g.renderer.setLogo(placeholderLogo());
}

static void chooseLogo() {
    runDialog(1,
              {"zenity", "--file-selection", "--title=Choose your logo (PNG with a transparent background works best)",
               "--file-filter=Images | *.png *.PNG *.jpg *.JPG *.jpeg *.JPEG *.gif *.GIF *.bmp *.BMP *.tga *.TGA",
               "--file-filter=All files | *"},
              {"kdialog", "--title", "Choose your logo", "--getopenfilename", homeDir(),
               "Images (*.png *.jpg *.jpeg *.gif *.bmp *.tga)"});
}

