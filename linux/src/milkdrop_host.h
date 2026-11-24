// Dropocalypse for Linux - plays MilkDrop presets with projectM.
//
// projectM gets its own OpenGL context (a hidden window sharing textures with the main one), so
// its state never mixes with ours. Each frame it draws into a texture the renderer samples directly.
#pragma once

#include "gl.h"
#include "milkdrop.h"

struct GLFWwindow;

class MilkdropHost {
public:
    MilkdropHost() = default;
    ~MilkdropHost();
    MilkdropHost(const MilkdropHost&) = delete;
    MilkdropHost& operator=(const MilkdropHost&) = delete;

    /// Loads projectM from beside the app (or the system) and makes its context, sharing with
    /// `mainWindow`'s. Safe to call again; false (with error()) if presets can't be played.
    bool init(GLFWwindow* mainWindow, const std::filesystem::path& libraryDir,
              const std::filesystem::path& bundledTextures);
    bool available() const { return pm_ != nullptr; }
    const std::string& error() const { return error_; }

    /// Switches to a preset. `smooth` blends from the one before, as MilkDrop does.
    void setPreset(const MilkPreset& preset, const std::filesystem::path& userDir, bool smooth);

    /// Draws one frame at width x height, fed with the newest mono samples. Returns the texture
    /// (shared with the main context) or 0. The main context is current again afterwards.
    GLuint render(int width, int height, const float* samples, int count);

private:
    void ensureSize(int width, int height);

    ProjectMApi api_;
    void* pm_ = nullptr;
    GLFWwindow* main_ = nullptr;
    GLFWwindow* ctx_ = nullptr;
    std::filesystem::path bundledTextures_;
    std::string error_;
    bool tried_ = false;
    GLuint fbo_ = 0, tex_ = 0;
    int width_ = 0, height_ = 0;
};
