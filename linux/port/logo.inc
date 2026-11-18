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

static bool copyItemFile(const std::wstring& from, const std::wstring& to) {
    return from == to || copyFile(narrow(from), narrow(to));
}

static void deleteItemFile(const std::wstring& path) { std::remove(narrow(path).c_str()); }

static std::wstring dataPath(const std::wstring& name) { return g.dataDir + L"/" + name; }
