// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

static bool cursorShouldHide() {
    return !g.toolbarVisible && !g.anyPopupOpen && nowSeconds() - g.lastMouseMove > kHideDelay;
}

/// Keyboard shortcuts shared by the visuals window and the controls window.
static bool handleKey(int key, int mods) {
    bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    switch (key) {
    case GLFW_KEY_RIGHT: nextScene(1); return true;
    case GLFW_KEY_LEFT: nextScene(-1); return true;
    case GLFW_KEY_F11: toggleFullscreen(); return true;
    case GLFW_KEY_ESCAPE:
        if (g.toolbarVisible && !g.anyPopupOpen && !g.settings.controlsDetached) g.toolbarVisible = false;
        else if (g.fullscreen) toggleFullscreen();
        return true;
    case GLFW_KEY_F: if (!ctrl) { toggleFullscreen(); return true; } break;
    case GLFW_KEY_T:
    case GLFW_KEY_S:
    case GLFW_KEY_H:
        if (!ctrl && !g.settings.controlsDetached) { g.toolbarVisible = !g.toolbarVisible; g.lastActivity = nowSeconds(); return true; }
        break;
    case GLFW_KEY_D: if (!ctrl) { g.wantDetach = g.settings.controlsDetached ? 0 : 1; return true; } break;
    case GLFW_KEY_R: if (!ctrl) { toggleRecording(); return true; } break;
    case GLFW_KEY_L: if (!ctrl) { chooseLogo(); return true; } break;
    case GLFW_KEY_O: if (ctrl) { chooseLogo(); return true; } break;
    case GLFW_KEY_Q: if (ctrl) { g.quitRequested = true; return true; } break;
    case GLFW_KEY_G: if (!ctrl) { g.settings.r.showLogo = !g.settings.r.showLogo; return true; } break;
    case GLFW_KEY_A: if (!ctrl) { g.settings.r.autoCycle = !g.settings.r.autoCycle; return true; } break;
    }
    return false;
}

/// Runs `fn` with the Dear ImGui context of the window an event belongs to.
template <typename Fn>
static void withContextOf(GLFWwindow* w, Fn fn) {
    ImGuiContext* ctx = (w == g.ctrlWindow) ? g.ctrlCtx : g.mainCtx;
    if (!ctx) return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx);
    fn();
    ImGui::SetCurrentContext(prev);
}

static void onCursorPos(GLFWwindow* w, double x, double y) {
    withContextOf(w, [&] { ImGui_ImplGlfw_CursorPosCallback(w, x, y); });
    if (w == g.window) {
        g.lastMouseMove = nowSeconds();
        noteActivity((float)y < 90.0f * g.mainDpi);
    }
}
static void onMouseButton(GLFWwindow* w, int button, int action, int mods) {
    withContextOf(w, [&] { ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods); });
    if (w == g.window && action == GLFW_PRESS) { g.lastMouseMove = nowSeconds(); noteActivity(true); }
}
static void onScroll(GLFWwindow* w, double dx, double dy) {
    withContextOf(w, [&] { ImGui_ImplGlfw_ScrollCallback(w, dx, dy); });
    if (w == g.window) { g.lastMouseMove = nowSeconds(); noteActivity(true); }
}
static void onKey(GLFWwindow* w, int key, int scancode, int action, int mods) {
    bool imguiWantsKeys = false;
    withContextOf(w, [&] {
        ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
        imguiWantsKeys = ImGui::GetIO().WantCaptureKeyboard;
    });
    if (action == GLFW_PRESS && !imguiWantsKeys) handleKey(key, mods);
}
static void onChar(GLFWwindow* w, unsigned int c) {
    withContextOf(w, [&] { ImGui_ImplGlfw_CharCallback(w, c); });
}
static void onFocus(GLFWwindow* w, int focused) {
    withContextOf(w, [&] { ImGui_ImplGlfw_WindowFocusCallback(w, focused); });
}
static void onCursorEnter(GLFWwindow* w, int entered) {
    withContextOf(w, [&] { ImGui_ImplGlfw_CursorEnterCallback(w, entered); });
}
static void onDrop(GLFWwindow*, int count, const char** paths) {
    for (int i = 0; i < count && i < kMaxOverlayItems; ++i)
        if (paths[i]) g.pendingDrop.push_back(paths[i]);
}
/// Layout scale and font raster scale for a window. On X11 window units are pixels, so the layout
/// follows the desktop scale; on Wayland window units are already scaled, so the layout stays 1x
/// and only the fonts are drawn at the higher pixel density.
static void windowScales(GLFWwindow* w, float& layout, float& raster) {
    float xs = 1, ys = 1;
    glfwGetWindowContentScale(w, &xs, &ys);
    if (xs <= 0) xs = 1;
    int ww = 0, wh = 0, fw = 0, fh = 0;
    glfwGetWindowSize(w, &ww, &wh);
    glfwGetFramebufferSize(w, &fw, &fh);
    float pixelsPerUnit = (ww > 0 && fw > 0) ? (float)fw / (float)ww : 1.0f;
    layout = std::max(0.5f, xs / pixelsPerUnit);
    raster = std::max(layout, xs);
}

static void onContentScale(GLFWwindow* w, float, float) {
    if (w == g.window) { windowScales(w, g.mainDpi, g.mainRaster); g.fontsDirty = true; }
    else if (w == g.ctrlWindow) { windowScales(w, g.ctrlDpi, g.ctrlRaster); g.ctrlFontsDirty = true; }
}
static void onClose(GLFWwindow* w) {
    if (w == g.ctrlWindow) {               // closing the controls window puts them back on top
        glfwSetWindowShouldClose(w, GLFW_FALSE);
        g.wantDetach = 0;
    }
}

static void installCallbacks(GLFWwindow* w) {
    glfwSetCursorPosCallback(w, onCursorPos);
    glfwSetMouseButtonCallback(w, onMouseButton);
    glfwSetScrollCallback(w, onScroll);
    glfwSetKeyCallback(w, onKey);
    glfwSetCharCallback(w, onChar);
    glfwSetWindowFocusCallback(w, onFocus);
    glfwSetCursorEnterCallback(w, onCursorEnter);
    glfwSetDropCallback(w, onDrop);
    glfwSetWindowContentScaleCallback(w, onContentScale);
    glfwSetWindowCloseCallback(w, onClose);
}

static void setWindowIcon(GLFWwindow* w) {
    int iw = 0, ih = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(kIconPng, (int)sizeof(kIconPng), &iw, &ih, &n, 4);
    if (!px) return;
    GLFWimage img{iw, ih, px};
    glfwSetWindowIcon(w, 1, &img);   // ignored on Wayland, which uses the .desktop file's icon
    stbi_image_free(px);
}

// ---------------------------------------------------------------------------------------------
// Detached controls window
// ---------------------------------------------------------------------------------------------

static void openControlsWindow() {
    if (g.ctrlWindow) { glfwFocusWindow(g.ctrlWindow); return; }
    // A tall panel on the main (primary) screen, near its right edge.
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    int wx = 0, wy = 0, ww = 1920, wh = 1080;
    if (mon) glfwGetMonitorWorkarea(mon, &wx, &wy, &ww, &wh);
    float scale = 1.0f;
    if (mon) { float sy; glfwGetMonitorContentScale(mon, &scale, &sy); }
    // GLFW window sizes are in screen coordinates; on Wayland/X11 with scaling these are logical.
    int w = 360, h = std::min(820, wh - 40);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    g.ctrlWindow = glfwCreateWindow(w, h, "Dropocalypse Controls", nullptr, g.window);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    if (!g.ctrlWindow) return;
    glfwSetWindowPos(g.ctrlWindow, wx + ww - (int)(w * scale) - 24, wy + 24);
    glfwSetWindowSizeLimits(g.ctrlWindow, 300, 360, GLFW_DONT_CARE, GLFW_DONT_CARE);
    setWindowIcon(g.ctrlWindow);
    installCallbacks(g.ctrlWindow);
    windowScales(g.ctrlWindow, g.ctrlDpi, g.ctrlRaster);

    glfwMakeContextCurrent(g.ctrlWindow);
    glfwSwapInterval(0);   // the visuals window paces the loop
    ImGuiContext* prev = ImGui::GetCurrentContext();
    g.ctrlCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g.ctrlCtx);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(g.ctrlWindow, false);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ImGui::SetCurrentContext(prev);
    glfwMakeContextCurrent(g.window);
    g.ctrlFontsDirty = true;

    glfwShowWindow(g.ctrlWindow);
    g.settings.controlsDetached = true;
    g.toolbarVisible = false;
}

static void closeControlsWindow() {
    g.settings.controlsDetached = false;
    if (!g.ctrlWindow) return;
    glfwMakeContextCurrent(g.ctrlWindow);
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g.ctrlCtx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(g.ctrlCtx);
    ImGui::SetCurrentContext(prev == g.ctrlCtx ? g.mainCtx : prev);
    g.ctrlCtx = nullptr;
    GLFWwindow* w = g.ctrlWindow;
    g.ctrlWindow = nullptr;
    glfwDestroyWindow(w);
    glfwMakeContextCurrent(g.window);
    noteActivity(true);
}

/// Draws one frame of the controls window (if it's open).
static void renderControlsWindow() {
    if (!g.ctrlWindow || glfwGetWindowAttrib(g.ctrlWindow, GLFW_ICONIFIED)) return;
    glfwMakeContextCurrent(g.ctrlWindow);
    ImGui::SetCurrentContext(g.ctrlCtx);
    g.dpi = g.ctrlDpi;
    g.raster = g.ctrlRaster;
    if (g.ctrlFontsDirty) { rebuildFonts(); g.ctrlFontsDirty = false; }
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(g.ctrlWindow, &fw, &fh);
    if (fw > 0 && fh > 0) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawControlsPanel();
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fw, fh);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g.ctrlWindow);
    }
    ImGui::SetCurrentContext(g.mainCtx);
    g.dpi = g.mainDpi;
    g.raster = g.mainRaster;
    glfwMakeContextCurrent(g.window);
}

// ---------------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------------

static void showFatal(const std::string& msg) {
    std::fprintf(stderr, "Dropocalypse: %s\n", msg.c_str());
    if (runProgram({"zenity", "--error", "--title=Dropocalypse", "--text=" + msg}) < 0)
        runProgram({"kdialog", "--title", "Dropocalypse", "--error", msg});
}

/// Saves the window's current picture as a binary PPM (used by --self-test in CI).
static bool savePpm(const std::string& path, int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) std::fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f);
    std::fclose(f);
    return true;
}

/// One Dropocalypse at a time (like the Windows version): a lock on a file in the settings folder.
/// The kernel drops it when the process ends. After an update the new copy waits for the old one.
static int gInstanceLock = -1;
static bool becomeTheOnlyInstance(const std::string& lockPath, bool updaterRelaunch) {
    int fd = open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return true;   // don't refuse to run if the lock file can't be made
    const int attempts = updaterRelaunch ? 100 : 1;   // up to 10 s after an update
    for (int i = 0; i < attempts; ++i) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) { gInstanceLock = fd; return true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    close(fd);
    return false;
}

int main(int argc, char** argv) {
    // --self-test[=screenshot.ppm]: start, draw ~3 seconds of frames, save a picture, quit.
    // CI runs this under a virtual display to make sure the OpenGL code works.
    bool selfTest = false, updated = false;
    std::string selfTestShot;
    // --preview=DIR: like --self-test, but saves the visuals (no toolbar) of every frame after a
    // warm-up into DIR as NNNN.ppm, with their times in DIR/times.txt. Used to make the README GIFs.
    std::string previewDir;
    double previewWarmup = 4.0, previewSeconds = 4.4;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--updated") updated = true;
        if (a.rfind("--preview=", 0) == 0) { previewDir = a.substr(10); selfTest = true; }
        if (a.rfind("--preview-seconds=", 0) == 0) previewSeconds = std::max(0.5, std::atof(a.c_str() + 18));
        if (a.rfind("--self-test", 0) == 0) {
            selfTest = true;
            if (a.size() > 12 && a[11] == '=') selfTestShot = a.substr(12);
        }
    }
    std::setlocale(LC_ALL, "");
    std::setlocale(LC_NUMERIC, "C");   // settings use "1.5", not "1,5"
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (selfTest) setenv("XDG_CONFIG_HOME", "/tmp/dropocalypse-selftest", 1);
    g.dataDir = appDataDir();
    if (!becomeTheOnlyInstance(narrow(g.dataDir) + "/instance.lock", updated)) {
        std::fprintf(stderr, "Dropocalypse is already running.\n");
        return 0;
    }
    g.settingsPath = g.dataDir + L"/settings.ini";
    g.settingsExisted = loadSettings(g.settingsPath, g.settings);
    g.lastSaved = serialize(g.settings);

    glfwSetErrorCallback([](int code, const char* desc) { std::fprintf(stderr, "GLFW error %d: %s\n", code, desc); });
    if (!glfwInit()) {
        showFatal("Couldn't open a window. Dropocalypse needs a desktop session (X11 or Wayland).");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "dropocalypse");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "dropocalypse");
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "dropocalypse");
    g.window = previewDir.empty() ? glfwCreateWindow(1280, 720, "Dropocalypse", nullptr, nullptr)
                                  : glfwCreateWindow(720, 406, "Dropocalypse", nullptr, nullptr);
    if (!g.window) {
        showFatal("Couldn't start OpenGL 3.3. Please update your graphics driver.");
        glfwTerminate();
        return 1;
    }
    glfwSetWindowSizeLimits(g.window, 640, 400, GLFW_DONT_CARE, GLFW_DONT_CARE);
    setWindowIcon(g.window);
    glfwMakeContextCurrent(g.window);
    glfwSwapInterval(1);
    windowScales(g.window, g.mainDpi, g.mainRaster);
    g.dpi = g.mainDpi;
    g.raster = g.mainRaster;

    std::wstring error;
    if (!g.renderer.init(error)) {
        showFatal(narrow(error));
        return 1;
    }
    // MilkDrop presets: optional. Without projectM the styles just don't appear.
    if (!g.milkHost.init(g.window, milkLibraryDir(), milkBundledDir() / "textures"))
        std::fprintf(stderr, "Dropocalypse: MilkDrop presets unavailable: %s\n", g.milkHost.error().c_str());
    refreshMilkdrop();

    IMGUI_CHECKVERSION();
    g.mainCtx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(g.window, false);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    installCallbacks(g.window);

    loadItems();

    g.sources = AudioCapture::listSources();
    startAudio();
    if (g.settings.checkForUpdates && g.settings.privacyNoticeSeen && !isDevVersion(APP_VERSION_STR) &&
        (long long)std::time(nullptr) - g.settings.lastUpdateCheck > 20 * 3600)
        startUpdateCheck(false);
    initWhatsNew();
    noteActivity(true);
    if (g.settings.controlsDetached) openControlsWindow();

    if (selfTest) { g.settings.privacyNoticeSeen = true; g.settings.checkForUpdates = false; g.whatsNewOpen = false; }

    double lastFrame = nowSeconds();
    double lastSaveCheck = lastFrame;
    bool running = true;
    int frameCount = 0;
    int exitCode = 0;

    while (running) {
        pollUpdateCheck();
        if (relaunchIfUpdated()) running = false;
        glfwPollEvents();
        if (glfwWindowShouldClose(g.window) || g.quitRequested) running = false;
        if (!running) break;

        pollDialog();
        if (!g.pendingDrop.empty()) {
            std::vector<std::string> dropped;
            dropped.swap(g.pendingDrop);
            for (const std::string& p : dropped) loadLogoFrom(widen(p));
        }
        if (g.wantDetach >= 0) {
            int want = g.wantDetach;
            g.wantDetach = -1;
            if (want == 1) openControlsWindow();
            else closeControlsWindow();
        }
        const bool iconic = glfwGetWindowAttrib(g.window, GLFW_ICONIFIED) != 0;
        if (const int hiddenSleep = hiddenFrameSleepMs(iconic, false)) {
            renderControlsWindow();
            std::this_thread::sleep_for(std::chrono::milliseconds(hiddenSleep));
            continue;
        }
        if (g.settings.frameCap == 30 || g.settings.frameCap == 60) {
            const double minDt = 1.0 / g.settings.frameCap;
            const double elapsed = nowSeconds() - g.lastPresent;
            if (g.lastPresent > 0 && elapsed < minDt) {
                std::this_thread::sleep_for(std::chrono::duration<double>(std::max(0.001, minDt - elapsed)));
                continue;
            }
        }

        ImGui::SetCurrentContext(g.mainCtx);
        g.dpi = g.mainDpi;
        g.raster = g.mainRaster;
        if (g.fontsDirty) { rebuildFonts(); g.fontsDirty = false; }
        glfwGetFramebufferSize(g.window, &g.width, &g.height);
        if (g.width <= 0 || g.height <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }

        double now = nowSeconds();
        float dt = (float)(now - lastFrame);
        lastFrame = now;
        if (dt > 0 && dt < 0.1) g.frameMs += ((dt * 1000.0) - g.frameMs) * 0.08;

        // Feed settings to the audio thread.
        g.audio.analysis.sensitivity.store(g.settings.sensitivity);
        g.audio.analysis.beatThreshold.store(g.settings.beatThreshold);

        // Visualizer.
        if (g.milkRescan) refreshMilkdrop();
        g.renderer.render(0, g.width, g.height, g.audio.analysis, g.audio.sampleRate.load(), g.settings.r);
        if (g.recorder.isRecording()) {   // before the toolbar is drawn on top
            const ContentRect frame = fitContentRect((float)g.width, (float)g.height, g.settings.r.portrait);
            g.recorder.captureFrame(g.width, g.height, frame.x, frame.y, frame.width, frame.height);
        }
        if (!previewDir.empty()) {
            static double previewStart = nowSeconds();
            static int previewIndex = 0;
            static FILE* times = nullptr;
            const double t = nowSeconds() - previewStart - previewWarmup;
            if (t >= 0 && t <= previewSeconds) {
                if (!times) times = std::fopen((previewDir + "/times.txt").c_str(), "w");
                char name[64];
                std::snprintf(name, sizeof(name), "/%04d.ppm", previewIndex);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                savePpm(previewDir + name, g.width, g.height);
                if (times) std::fprintf(times, "%d %.4f\n", previewIndex, t);
                ++previewIndex;
            } else if (t > previewSeconds) {
                if (times) std::fclose(times);
                std::printf("preview: %d frames, %dx%d, style %s, milkdrop %s\n", previewIndex, g.width, g.height,
                            g.renderer.sceneName(), g.milkHost.available() ? "on" : "off");
                running = false;
            }
        }

        // Toolbar auto-hide.
        if (cursorShouldHide()) io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        g.anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (g.settings.controlsDetached) g.toolbarVisible = false;
        g.pointerOverToolbar = g.toolbarVisible && io.MousePos.y >= 0 && io.MousePos.y < toolbarHeight();
        if (g.anyPopupOpen || g.pointerOverToolbar) g.lastActivity = now;
        if (g.toolbarVisible && now - g.lastActivity > kHideDelay) g.toolbarVisible = false;

        if (!g.settings.controlsDetached) drawToolbar(dt);
        drawPrivacyNotice();
        drawWhatsNew();
        drawNotices();

        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, g.width, g.height);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (selfTest && previewDir.empty() && ++frameCount == 180) {
            GLenum err = glGetError();
            if (!selfTestShot.empty()) savePpm(selfTestShot, g.width, g.height);
            std::printf("self-test: %d frames, %dx%d, GL error 0x%x, style %s, items %d, milkdrop %s (%d presets)\n",
                        frameCount, g.width, g.height, (unsigned)err, g.renderer.sceneName(),
                        (int)g.settings.items.size(), g.milkHost.available() ? "on" : "off",
                        (int)g.milkCatalog.presets.size());
            exitCode = err == GL_NO_ERROR ? 0 : 2;
            running = false;
        }
        glfwSetInputMode(g.window, GLFW_CURSOR, cursorShouldHide() ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
        glfwSwapBuffers(g.window);
        g.lastPresent = nowSeconds();

        renderControlsWindow();

        // Save settings when something changed (checked twice a second).
        if (now - lastSaveCheck > 0.5) {
            lastSaveCheck = now;
            std::string s = serialize(g.settings);
            if (s != g.lastSaved) {
                writeFile(g.settingsPath, s);
                g.lastSaved = s;
            }
        }
    }

    if (g.recorder.isRecording()) stopRecording();
    g.recorder.waitIdle();
    bool keepDetached = g.settings.controlsDetached;
    closeControlsWindow();
    g.settings.controlsDetached = keepDetached;   // reopen detached next time
    writeFile(g.settingsPath, serialize(g.settings));
    g.audio.stop();
    ImGui::SetCurrentContext(g.mainCtx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g.window);
    glfwTerminate();
    curl_global_cleanup();
    return exitCode;
}

