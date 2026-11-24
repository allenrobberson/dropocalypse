// Dropocalypse for Windows - MilkDrop presets through projectM (see milkdrop_host.h).
#include "milkdrop_host.h"

#include <GL/gl.h>

#include <cstring>

namespace {

// OpenGL past 1.1 comes through wglGetProcAddress.
constexpr GLenum kFramebuffer = 0x8D40, kReadFramebuffer = 0x8CA8, kColorAttachment0 = 0x8CE0,
                 kFramebufferComplete = 0x8CD5, kPixelPackBuffer = 0x88EB, kStreamRead = 0x88E1,
                 kReadOnly = 0x88B8, kBgra = 0x80E1, kRgba8 = 0x8058;

using PFNGenFramebuffers = void(APIENTRY*)(GLsizei, GLuint*);
using PFNDeleteFramebuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using PFNBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using PFNFramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFNCheckFramebufferStatus = GLenum(APIENTRY*)(GLenum);
using PFNGenBuffers = void(APIENTRY*)(GLsizei, GLuint*);
using PFNDeleteBuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using PFNBindBuffer = void(APIENTRY*)(GLenum, GLuint);
using PFNBufferData = void(APIENTRY*)(GLenum, ptrdiff_t, const void*, GLenum);
using PFNMapBuffer = void*(APIENTRY*)(GLenum, GLenum);
using PFNUnmapBuffer = GLboolean(APIENTRY*)(GLenum);
using PFNCreateContextAttribs = HGLRC(WINAPI*)(HDC, HGLRC, const int*);

PFNGenFramebuffers glGenFramebuffers_;
PFNDeleteFramebuffers glDeleteFramebuffers_;
PFNBindFramebuffer glBindFramebuffer_;
PFNFramebufferTexture2D glFramebufferTexture2D_;
PFNCheckFramebufferStatus glCheckFramebufferStatus_;
PFNGenBuffers glGenBuffers_;
PFNDeleteBuffers glDeleteBuffers_;
PFNBindBuffer glBindBuffer_;
PFNBufferData glBufferData_;
PFNMapBuffer glMapBuffer_;
PFNUnmapBuffer glUnmapBuffer_;

template <typename T> bool getGl(T& fn, const char* name) {
    fn = reinterpret_cast<T>(wglGetProcAddress(name));
    return fn != nullptr;
}

LRESULT CALLBACK hiddenProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

} // namespace

MilkdropHost::~MilkdropHost() { release(); }

void MilkdropHost::release() {
    if (glrc_ && hdc_) {
        wglMakeCurrent(hdc_, glrc_);
        if (pm_ && api_.destroy) api_.destroy(pm_);
        pm_ = nullptr;
        if (pbo_[0] && glDeleteBuffers_) glDeleteBuffers_(2, pbo_);
        if (fbo_ && glDeleteFramebuffers_) glDeleteFramebuffers_(1, &fbo_);
        if (colorTex_) glDeleteTextures(1, &colorTex_);
        pbo_[0] = pbo_[1] = fbo_ = colorTex_ = 0;
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(glrc_);
    }
    glrc_ = nullptr;
    if (hdc_ && hwnd_) ReleaseDC(hwnd_, hdc_);
    hdc_ = nullptr;
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    width_ = height_ = 0;
}

bool MilkdropHost::makeContext() {
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = hiddenProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DropocalypseMilkdropGL";
    RegisterClassW(&wc); // fails harmlessly if already registered
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd_) return false;
    hdc_ = GetDC(hwnd_);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    int format = ChoosePixelFormat(hdc_, &pfd);
    if (!format || !SetPixelFormat(hdc_, format, &pfd)) return false;

    // A plain context first, to reach wglCreateContextAttribsARB for a 3.3 core context.
    HGLRC legacy = wglCreateContext(hdc_);
    if (!legacy || !wglMakeCurrent(hdc_, legacy)) {
        if (legacy) wglDeleteContext(legacy);
        return false;
    }
    PFNCreateContextAttribs createAttribs = nullptr;
    getGl(createAttribs, "wglCreateContextAttribsARB");
    if (createAttribs) {
        const int attribs[] = {0x2091, 3,      // WGL_CONTEXT_MAJOR_VERSION_ARB
                               0x2092, 3,      // WGL_CONTEXT_MINOR_VERSION_ARB
                               0x9126, 0x0001, // WGL_CONTEXT_PROFILE_MASK_ARB = core
                               0};
        HGLRC core = createAttribs(hdc_, nullptr, attribs);
        if (core) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(legacy);
            legacy = core;
        }
    }
    glrc_ = legacy;
    if (!wglMakeCurrent(hdc_, glrc_)) return false;

    bool ok = getGl(glGenFramebuffers_, "glGenFramebuffers") && getGl(glDeleteFramebuffers_, "glDeleteFramebuffers") &&
              getGl(glBindFramebuffer_, "glBindFramebuffer") &&
              getGl(glFramebufferTexture2D_, "glFramebufferTexture2D") &&
              getGl(glCheckFramebufferStatus_, "glCheckFramebufferStatus") && getGl(glGenBuffers_, "glGenBuffers") &&
              getGl(glDeleteBuffers_, "glDeleteBuffers") && getGl(glBindBuffer_, "glBindBuffer") &&
              getGl(glBufferData_, "glBufferData") && getGl(glMapBuffer_, "glMapBuffer") &&
              getGl(glUnmapBuffer_, "glUnmapBuffer");
    return ok;
}

bool MilkdropHost::init(ID3D11Device* device, ID3D11DeviceContext* ctx, const std::filesystem::path& libraryDir,
                        const std::filesystem::path& bundledTextures) {
    if (pm_) return true;
    if (tried_) return false;
    tried_ = true;
    device_ = device;
    ctx_ = ctx;
    bundledTextures_ = bundledTextures;

    if (!api_.load({libraryDir / "projectM-4.dll"})) {
        error_ = api_.error;
        return false;
    }
    if (!makeContext()) {
        error_ = "This PC's graphics driver doesn't offer OpenGL 3.3, which MilkDrop presets need.";
        release();
        return false;
    }
    // projectM reaches OpenGL through GLEW, which has to be set up in this context first.
    if (HMODULE glew = GetModuleHandleW(L"glew32.dll")) {
        if (auto* experimental = reinterpret_cast<unsigned char*>(GetProcAddress(glew, "glewExperimental")))
            *experimental = 1;
        using GlewInit = unsigned(__stdcall*)();
        if (auto init = reinterpret_cast<GlewInit>(GetProcAddress(glew, "glewInit"))) init();
    }
    while (glGetError() != GL_NO_ERROR) {
    }

    pm_ = api_.create();
    if (!pm_) {
        error_ = "The MilkDrop engine couldn't start with this PC's graphics driver.";
        release();
        return false;
    }
    api_.setPresetLocked(pm_, true);
    api_.setHardCutEnabled(pm_, false);
    api_.setPresetDuration(pm_, 1e9);
    api_.setSoftCutDuration(pm_, 2.5);
    api_.setFps(pm_, 60);
    return true;
}

void MilkdropHost::setPreset(const MilkPreset& preset, const std::filesystem::path& userDir, bool smooth) {
    if (!pm_) return;
    std::string data = readMilkFile(preset.path);
    if (data.empty()) return;
    wglMakeCurrent(hdc_, glrc_);
    std::vector<std::string> paths = milkTexturePaths(preset.path, userDir, bundledTextures_);
    std::vector<const char*> ptrs;
    for (const auto& p : paths) ptrs.push_back(p.c_str());
    api_.setTextureSearchPaths(pm_, ptrs.data(), ptrs.size());
    api_.loadPresetData(pm_, data.c_str(), smooth);
}

void MilkdropHost::ensureSize(int width, int height) {
    if (width == width_ && height == height_ && fbo_) return;
    width_ = width;
    height_ = height;
    if (!colorTex_) glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, kRgba8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!fbo_) glGenFramebuffers_(1, &fbo_);
    glBindFramebuffer_(kFramebuffer, fbo_);
    glFramebufferTexture2D_(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, colorTex_, 0);
    glBindFramebuffer_(kFramebuffer, 0);
    if (!pbo_[0]) glGenBuffers_(2, pbo_);
    for (unsigned b : pbo_) {
        glBindBuffer_(kPixelPackBuffer, b);
        glBufferData_(kPixelPackBuffer, (ptrdiff_t)width * height * 4, nullptr, kStreamRead);
    }
    glBindBuffer_(kPixelPackBuffer, 0);
    pboFilled_[0] = pboFilled_[1] = false;
    haveFrame_ = false;
    api_.setWindowSize(pm_, (size_t)width, (size_t)height);

    tex_.Reset();
    srv_.Reset();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = width;
    d.Height = height;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(device_->CreateTexture2D(&d, nullptr, &tex_))) device_->CreateShaderResourceView(tex_.Get(), nullptr, &srv_);
}

ID3D11ShaderResourceView* MilkdropHost::render(int width, int height, const float* samples, int count) {
    if (!pm_ || width < 16 || height < 16) return nullptr;
    if (!wglMakeCurrent(hdc_, glrc_)) return nullptr;
    ensureSize(width, height);
    if (!tex_ || glCheckFramebufferStatus_ == nullptr) return nullptr;

    if (samples && count > 0) api_.pcmAddFloat(pm_, samples, (unsigned)count, 1);
    api_.renderFrameFbo(pm_, fbo_);

    // Start reading this frame into one buffer...
    glBindFramebuffer_(kReadFramebuffer, fbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindBuffer_(kPixelPackBuffer, pbo_[pboIndex_]);
    glReadPixels(0, 0, width, height, kBgra, GL_UNSIGNED_BYTE, nullptr);
    pboFilled_[pboIndex_] = true;
    glBindFramebuffer_(kReadFramebuffer, 0);

    // ...and copy last frame's buffer, which is ready by now, into Direct3D (flipping it, since
    // OpenGL's rows run bottom to top).
    const int other = 1 - pboIndex_;
    if (pboFilled_[other]) {
        glBindBuffer_(kPixelPackBuffer, pbo_[other]);
        if (const auto* src = static_cast<const uint8_t*>(glMapBuffer_(kPixelPackBuffer, kReadOnly))) {
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(ctx_->Map(tex_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                const size_t row = (size_t)width * 4;
                for (int y = 0; y < height; ++y)
                    std::memcpy(static_cast<uint8_t*>(mapped.pData) + (size_t)y * mapped.RowPitch,
                                src + (size_t)(height - 1 - y) * row, row);
                ctx_->Unmap(tex_.Get(), 0);
                haveFrame_ = true;
            }
            glUnmapBuffer_(kPixelPackBuffer);
        }
    }
    glBindBuffer_(kPixelPackBuffer, 0);
    pboIndex_ = other;
    return haveFrame_ ? srv_.Get() : nullptr;
}
