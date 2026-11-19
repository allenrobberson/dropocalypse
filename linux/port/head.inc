// Dropocalypse for Linux - window, toolbar, settings and the main loop.
//
// The same app as the Windows version (whose UI code this shares): GLFW windows with an OpenGL 3.3
// context, Dear ImGui for the toolbar and the detached controls window, PulseAudio for sound.

#include "audio.h"
#include "core_logic.h"
#include "whats_new_notes.h"
#include "recorder.h"
#include "renderer.h"

#include <GLFW/glfw3.h>
#include <curl/curl.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

#include "icon_png.h"
#include "stb_image.h"

// ---------------------------------------------------------------------------------------------
// Small Linux helpers
// ---------------------------------------------------------------------------------------------

static std::string homeDir() {
    const char* h = std::getenv("HOME");
    return h && *h ? h : "/tmp";
}

static void makeDirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty()) mkdir(cur.c_str(), 0755);
        }
        if (i < path.size()) cur.push_back(path[i]);
    }
}

static bool fileExists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }

/// Runs a program without a shell and waits for it. Returns its exit code (-1 if it couldn't run).
static int runProgram(const std::vector<std::string>& args, std::string* output = nullptr) {
    std::vector<char*> argv;
    std::vector<std::string> copy = args;
    for (auto& a : copy) argv.push_back(a.data());
    argv.push_back(nullptr);
    int fds[2] = {-1, -1};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (output && pipe(fds) == 0) {
        posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
        posix_spawn_file_actions_addclose(&fa, fds[0]);
    }
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (fds[1] >= 0) close(fds[1]);
    if (rc != 0) {
        if (fds[0] >= 0) close(fds[0]);
        return -1;
    }
    if (output && fds[0] >= 0) {
        char buf[4096];
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof(buf))) > 0) output->append(buf, (size_t)n);
        close(fds[0]);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/// Starts a program in the background (not waited for).
static bool launchDetached(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    std::vector<std::string> copy = args;
    for (auto& a : copy) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return false;
    // Reap it later without blocking.
    std::thread([pid] { int st; waitpid(pid, &st, 0); }).detach();
    return true;
}

/// Opens a web page, file or folder with the desktop's default app.
static void openWithDesktop(const std::string& target) { launchDetached({"xdg-open", target}); }

// ---------------------------------------------------------------------------------------------
// Settings (UTF-8 "key=value" file in ~/.config/dropocalypse/settings.ini)
// ---------------------------------------------------------------------------------------------
