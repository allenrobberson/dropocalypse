// Dropocalypse for Linux - MilkDrop presets through projectM (see milkdrop_host.h).
#include "milkdrop_host.h"

#include <GLFW/glfw3.h>

MilkdropHost::~MilkdropHost() {
    if (ctx_) {
        glfwMakeContextCurrent(ctx_);
        if (pm_ && api_.destroy) api_.destroy(pm_);
        if (fbo_) glDeleteFramebuffers(1, &fbo_);
        if (tex_) glDeleteTextures(1, &tex_);
        glfwMakeContextCurrent(main_);
        glfwDestroyWindow(ctx_);
    }
}

bool MilkdropHost::init(GLFWwindow* mainWindow, const std::filesystem::path& libraryDir,
                        const std::filesystem::path& bundledTextures) {
    if (pm_) return true;
    if (tried_) return false;
    tried_ = true;
    main_ = mainWindow;
    bundledTextures_ = bundledTextures;

    // Next to the program (the .tar.gz), in ../lib (the AppImage), then the system's own copy.
    if (!api_.load({libraryDir / "libprojectM-4.so.4", libraryDir / ".." / "lib" / "libprojectM-4.so.4",
                    "libprojectM-4.so.4", "libprojectM-4.so"})) {
        error_ = api_.error;
        return false;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    ctx_ = glfwCreateWindow(16, 16, "Dropocalypse MilkDrop", nullptr, mainWindow);
    glfwDefaultWindowHints();
    if (!ctx_) {
        error_ = "This computer's graphics driver couldn't make a second OpenGL context for MilkDrop presets.";
        return false;
    }
    glfwMakeContextCurrent(ctx_);
    pm_ = api_.create();
    if (pm_) {
        api_.setPresetLocked(pm_, true);
        api_.setHardCutEnabled(pm_, false);
        api_.setPresetDuration(pm_, 1e9);
        api_.setSoftCutDuration(pm_, 2.5);
        api_.setFps(pm_, 60);
    } else {
        error_ = "The MilkDrop engine couldn't start with this computer's graphics driver.";
    }
    glfwMakeContextCurrent(main_);
    return pm_ != nullptr;
}

void MilkdropHost::setPreset(const MilkPreset& preset, const std::filesystem::path& userDir, bool smooth) {
    if (!pm_) return;
    std::string data = readMilkFile(preset.path);
    if (data.empty()) return;
    glfwMakeContextCurrent(ctx_);
    std::vector<std::string> paths = milkTexturePaths(preset.path, userDir, bundledTextures_);
    std::vector<const char*> ptrs;
    for (const auto& p : paths) ptrs.push_back(p.c_str());
    api_.setTextureSearchPaths(pm_, ptrs.data(), ptrs.size());
    api_.loadPresetData(pm_, data.c_str(), smooth);
    glfwMakeContextCurrent(main_);
}

void MilkdropHost::ensureSize(int width, int height) {
    if (width == width_ && height == height_ && fbo_) return;
    width_ = width;
    height_ = height;
    if (!tex_) glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!fbo_) glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    api_.setWindowSize(pm_, (size_t)width, (size_t)height);
}

GLuint MilkdropHost::render(int width, int height, const float* samples, int count) {
    if (!pm_ || width < 16 || height < 16) return 0;
    glfwMakeContextCurrent(ctx_);
    ensureSize(width, height);
    if (samples && count > 0) api_.pcmAddFloat(pm_, samples, (unsigned)count, 1);
    api_.renderFrameFbo(pm_, fbo_);
    glFinish(); // the main context reads the texture next
    glfwMakeContextCurrent(main_);
    return tex_;
}
