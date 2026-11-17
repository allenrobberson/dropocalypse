static std::wstring appDataDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base = xdg && *xdg ? std::string(xdg) : homeDir() + "/.config";
    std::string dir = base + "/dropocalypse";
    makeDirs(dir);
    return widen(dir);
}

