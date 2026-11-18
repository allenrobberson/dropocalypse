// ---------------------------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------------------------

struct App {
    GLFWwindow* window = nullptr;
    int width = 0, height = 0;   // framebuffer size in pixels
    double frameMs = 0;          // smoothed frame time, shown next to the audio status
    double lastPresent = 0;

    Renderer renderer;
    AudioCapture audio;
    Recorder recorder;
    std::mutex recordMutex;
    std::wstring recordNotice;      // "Saved ..." / errors, shown for a few seconds
    double recordNoticeUntil = 0;
    std::wstring lastRecording;
    bool recordNoticePending = false;
    std::wstring pendingNotice;
    std::vector<AudioSource> sources;
    AppSettings settings;
    std::wstring dataDir, settingsPath;
    std::string lastSaved;
    std::wstring errorMessage;

    // Toolbar auto-hide.
    float dpi = 1.0f;          // layout scale for sizes in the UI (in window units)
    float raster = 1.0f;       // pixels per window unit times the desktop scale: fonts are drawn at this size
    bool fontsDirty = true;
    bool toolbarVisible = true;
    float toolbarAnim = 1.0f;
    double lastActivity = 0, lastMouseMove = 0;
    bool anyPopupOpen = false;
    bool pointerOverToolbar = false;

    // Detached controls window: its own GLFW window (sharing the OpenGL objects) and Dear ImGui
    // context, so the toolbar can sit on the main screen while the visuals run full screen on
    // another display.
    float mainDpi = 1.0f, mainRaster = 1.0f;
    ImGuiContext* mainCtx = nullptr;
    GLFWwindow* ctrlWindow = nullptr;
    ImGuiContext* ctrlCtx = nullptr;
    float ctrlDpi = 1.0f, ctrlRaster = 1.0f;
    bool ctrlFontsDirty = true;
    int wantDetach = -1;          // deferred: 1 = open the controls window, 0 = close it
    bool ctrlCloseRequested = false;

    // Full screen.
    bool fullscreen = false;
    int prevX = 100, prevY = 100, prevW = 1280, prevH = 720;

    std::vector<std::string> pendingDrop;   // files dropped on a window, loaded on the next frame
    std::vector<LogoImage> itemImages;      // one per settings.items entry, as the renderer shows them
    int pendingReplace = -1;                // the next chosen image replaces this item instead of adding one
    bool quitRequested = false;
};

static App g;

