static void writeFile(const std::wstring& path, const std::string& text) {
    // Write a temporary file and rename it over the old one, so a crash never leaves half a file.
    std::string p = narrow(path), tmp = p + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (ok) std::rename(tmp.c_str(), p.c_str());
    else std::remove(tmp.c_str());
}

