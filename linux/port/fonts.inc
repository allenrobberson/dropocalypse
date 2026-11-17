// ---------------------------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------------------------

static void rebuildFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x205E, 0x2190, 0x21FF, 0x2300, 0x23FF, 0};
    const char* fonts[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
    };
    ImFont* f = nullptr;
    for (const char* path : fonts) {
        if (!fileExists(path)) continue;
        f = io.Fonts->AddFontFromFileTTF(path, 16.0f * g.raster, nullptr, ranges);
        if (f) break;
    }
    if (!f) {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * g.raster;
        io.Fonts->AddFontDefault(&cfg);
    }
    io.FontGlobalScale = g.dpi / g.raster;   // sharp text on scaled (HiDPI) screens

    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0;
    style.PopupRounding = 8;
    style.FrameRounding = 6;
    style.GrabRounding = 6;
    style.WindowBorderSize = 0;
    style.PopupBorderSize = 1;
    style.ItemSpacing = ImVec2(8, 8);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);
    style.Colors[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.06f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, 0.16f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(1, 1, 1, 0.26f);
    style.ScaleAllSizes(g.dpi);
    ImGui::GetStyle() = style;

    ImGui_ImplOpenGL3_DestroyDeviceObjects();   // recreated (with the new fonts) on the next frame
}

