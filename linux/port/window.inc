// ---------------------------------------------------------------------------------------------
// Window, full screen and displays
// ---------------------------------------------------------------------------------------------

static GLFWmonitor* monitorForWindow(GLFWwindow* w) {
    if (GLFWmonitor* m = glfwGetWindowMonitor(w)) return m;
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(w, &wx, &wy);
    glfwGetWindowSize(w, &ww, &wh);
    int cx = wx + ww / 2, cy = wy + wh / 2;
    int count = 0;
    GLFWmonitor** mons = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        int mx, my;
        glfwGetMonitorPos(mons[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(mons[i]);
        if (mode && cx >= mx && cx < mx + mode->width && cy >= my && cy < my + mode->height) return mons[i];
    }
    return glfwGetPrimaryMonitor();
}

static void enterFullscreen(GLFWmonitor* mon) {
    if (!mon) mon = monitorForWindow(g.window);
    const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr;
    if (!mode) return;
    if (!g.fullscreen) {
        glfwGetWindowPos(g.window, &g.prevX, &g.prevY);
        glfwGetWindowSize(g.window, &g.prevW, &g.prevH);
    }
    // Borderless at the screen's own resolution and refresh rate.
    glfwSetWindowMonitor(g.window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
    g.fullscreen = true;
}

static void leaveFullscreen() {
    if (!g.fullscreen) return;
    glfwSetWindowMonitor(g.window, nullptr, g.prevX, g.prevY, g.prevW, g.prevH, 0);
    g.fullscreen = false;
}

static void toggleFullscreen() {
    if (g.fullscreen) leaveFullscreen();
    else enterFullscreen(nullptr);
}

// ---- Displays: "Show visuals on" picks the screen the visuals go full screen on.
struct DisplayInfo {
    GLFWmonitor* handle;
    bool primary;
    std::string name;
};

static std::vector<DisplayInfo> listDisplays() {
    std::vector<DisplayInfo> out;
    int count = 0;
    GLFWmonitor** mons = glfwGetMonitors(&count);
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    for (int i = 0; i < count; ++i) {
        const GLFWvidmode* mode = glfwGetVideoMode(mons[i]);
        const char* mname = glfwGetMonitorName(mons[i]);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s  (%d x %d)%s", mname && *mname ? mname : "Display", mode ? mode->width : 0,
                      mode ? mode->height : 0, mons[i] == primary ? "  - main" : "");
        out.push_back({mons[i], mons[i] == primary, buf});
    }
    return out;
}

/// Moves the visuals window to a display and makes it full screen there.
static void showVisualsOn(const DisplayInfo& d) { enterFullscreen(d.handle); }

static GLFWmonitor* visualsMonitor() { return monitorForWindow(g.window); }

