// Dropocalypse for Windows - plays MilkDrop presets with projectM and hands each frame to Direct3D.
//
// projectM draws with OpenGL, so it gets its own small OpenGL context on a hidden window. Each
// frame is read back through a pixel buffer (one frame behind, so the GPU never waits) and copied
// into a Direct3D texture that the renderer blends with its own picture.
#pragma once

#include "milkdrop.h"

#include <d3d11.h>
#include <wrl/client.h>

class MilkdropHost {
public:
    MilkdropHost() = default;
    ~MilkdropHost();
    MilkdropHost(const MilkdropHost&) = delete;
    MilkdropHost& operator=(const MilkdropHost&) = delete;

    /// Loads projectM from `libraryDir` and makes the OpenGL context. Safe to call again; returns
    /// false (with error()) if MilkDrop presets can't be played on this PC.
    bool init(ID3D11Device* device, ID3D11DeviceContext* ctx, const std::filesystem::path& libraryDir,
              const std::filesystem::path& bundledTextures);
    bool available() const { return pm_ != nullptr; }
    const std::string& error() const { return error_; }

    /// Switches to a preset. `smooth` blends from the one before, as MilkDrop does.
    void setPreset(const MilkPreset& preset, const std::filesystem::path& userDir, bool smooth);

    /// Draws one frame at width x height, fed with the newest mono samples. Returns the picture
    /// (top row first) or nullptr.
    ID3D11ShaderResourceView* render(int width, int height, const float* samples, int count);

private:
    bool makeContext();
    void ensureSize(int width, int height);
    void release();

    template <typename T> using Com = Microsoft::WRL::ComPtr<T>;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    Com<ID3D11Texture2D> tex_;
    Com<ID3D11ShaderResourceView> srv_;

    ProjectMApi api_;
    void* pm_ = nullptr;
    std::filesystem::path bundledTextures_;
    std::string error_;
    bool tried_ = false;

    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC glrc_ = nullptr;
    unsigned fbo_ = 0, colorTex_ = 0, pbo_[2] = {0, 0};
    int width_ = 0, height_ = 0, pboIndex_ = 0;
    bool pboFilled_[2] = {false, false};
    bool haveFrame_ = false;
};
