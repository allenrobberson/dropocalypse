// ---------------------------------------------------------------------------------------------
// File dialogs: zenity (GNOME, Xfce, ...) or kdialog (KDE), run in the background so the visuals
// keep moving while the dialog is open. The answer is picked up on the next frame.
// ---------------------------------------------------------------------------------------------

struct DialogState {
    std::mutex m;
    std::atomic<bool> busy{false};
    bool ready = false;
    int kind = 0;            // 1 = logo file, 2 = recording folder
    std::string result;
};
static DialogState gDialog;

static bool haveProgram(const char* name) {
    std::string out;
    return runProgram({"sh", "-c", std::string("command -v ") + name}, &out) == 0 && !out.empty();
}

static void runDialog(int kind, std::vector<std::string> zenity, std::vector<std::string> kdialog) {
    if (gDialog.busy.exchange(true)) return;
    std::thread([kind, zenity, kdialog] {
        std::string out;
        int rc = -1;
        bool kde = std::getenv("KDE_FULL_SESSION") != nullptr;
        if (kde && haveProgram("kdialog")) rc = runProgram(kdialog, &out);
        else if (haveProgram("zenity")) rc = runProgram(zenity, &out);
        else if (haveProgram("kdialog")) rc = runProgram(kdialog, &out);
        else rc = -2;
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        std::lock_guard<std::mutex> lock(gDialog.m);
        gDialog.kind = kind;
        gDialog.result = rc == 0 ? out : (rc == -2 ? std::string("\x01nodialog") : std::string());
        gDialog.ready = true;
        gDialog.busy = false;
    }).detach();
}

