// Dropocalypse for Windows and Linux - MilkDrop presets, played by projectM.
//
// projectM (https://github.com/projectM-visualizer/projectm, LGPL-2.1) is loaded at run time
// from the library shipped next to the app. If it is missing, the MilkDrop styles simply don't
// appear. Only its C API is used, declared here so the app builds without projectM's headers.
//
// Presets come from two places: the ones bundled with the app, and a folder the user picks
// (searched with its subfolders, so a whole collection such as a NestDrop "Presets" folder works).
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/// The parts of projectM's C API that Dropocalypse uses.
struct ProjectMApi {
    using Handle = void*;
    Handle (*create)() = nullptr;
    void (*destroy)(Handle) = nullptr;
    void (*loadPresetData)(Handle, const char*, bool) = nullptr;
    void (*renderFrameFbo)(Handle, uint32_t) = nullptr;
    void (*pcmAddFloat)(Handle, const float*, unsigned int, int) = nullptr;
    void (*setWindowSize)(Handle, size_t, size_t) = nullptr;
    void (*setTextureSearchPaths)(Handle, const char**, size_t) = nullptr;
    void (*setPresetLocked)(Handle, bool) = nullptr;
    void (*setPresetDuration)(Handle, double) = nullptr;
    void (*setHardCutEnabled)(Handle, bool) = nullptr;
    void (*setSoftCutDuration)(Handle, double) = nullptr;
    void (*setFps)(Handle, int32_t) = nullptr;
    void (*setMeshSize)(Handle, size_t, size_t) = nullptr;

    bool loaded = false;
    std::string error;
    void* lib = nullptr;

    /// Tries each path in turn. True when every function was found.
    bool load(const std::vector<std::filesystem::path>& candidates) {
        if (loaded) return true;
        for (const auto& path : candidates) {
#ifdef _WIN32
            // Let the DLL find glew32.dll beside it.
            HMODULE h = LoadLibraryExW(path.wstring().c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            lib = (void*)h;
#else
            lib = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
            if (lib) break;
        }
        if (!lib) {
            error = "The MilkDrop engine (projectM) was not found next to the app.";
            return false;
        }
        bool ok = true;
        auto get = [&](auto& fn, const char* name) {
#ifdef _WIN32
            void* p = (void*)GetProcAddress((HMODULE)lib, name);
#else
            void* p = dlsym(lib, name);
#endif
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);
            if (!p) ok = false;
        };
        get(create, "projectm_create");
        get(destroy, "projectm_destroy");
        get(loadPresetData, "projectm_load_preset_data");
        get(renderFrameFbo, "projectm_opengl_render_frame_fbo");
        get(pcmAddFloat, "projectm_pcm_add_float");
        get(setWindowSize, "projectm_set_window_size");
        get(setTextureSearchPaths, "projectm_set_texture_search_paths");
        get(setPresetLocked, "projectm_set_preset_locked");
        get(setPresetDuration, "projectm_set_preset_duration");
        get(setHardCutEnabled, "projectm_set_hard_cut_enabled");
        get(setSoftCutDuration, "projectm_set_soft_cut_duration");
        get(setFps, "projectm_set_fps");
        get(setMeshSize, "projectm_set_mesh_size");
        if (!ok) error = "The MilkDrop engine (projectM) is too old or incomplete.";
        loaded = ok;
        return ok;
    }
};

/// UTF-8 text of a path (C++17 and C++20 alike).
inline std::string pathUtf8(const std::filesystem::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

struct MilkPreset {
    std::string name;            // shown in the style list
    std::filesystem::path path;  // the .milk file
    bool bundled = false;
};

inline std::string milkLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

/// Bundled presets (sorted by file name) followed by every .milk file under the user's folder
/// (sorted by path, case-insensitively). Stops after `limit` user presets.
struct MilkCatalog {
    std::vector<MilkPreset> presets;
    int bundledCount = 0;
    bool userTruncated = false;

    static constexpr int kUserLimit = 20000;

    static std::vector<MilkPreset> scan(const std::filesystem::path& dir, bool recursive, bool bundled,
                                        int limit, bool* truncated) {
        namespace fs = std::filesystem;
        std::vector<MilkPreset> out;
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) return out;
        auto consider = [&](const fs::directory_entry& e) {
            std::error_code ec2;
            if (!e.is_regular_file(ec2)) return true;
            if (milkLower(e.path().extension().string()) != ".milk") return true;
            if ((int)out.size() >= limit) {
                if (truncated) *truncated = true;
                return false;
            }
            MilkPreset p;
            p.path = e.path();
            p.name = pathUtf8(e.path().stem());
            // Bundled files are named "01 Acid Mandala" to keep their order; show just the name.
            if (bundled && p.name.size() > 3 && std::isdigit((unsigned char)p.name[0]) &&
                std::isdigit((unsigned char)p.name[1]) && p.name[2] == ' ')
                p.name = p.name.substr(3);
            p.bundled = bundled;
            out.push_back(std::move(p));
            return true;
        };
        if (recursive) {
            for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
                if (!consider(*it)) break;
        } else {
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
                if (!consider(*it)) break;
        }
        std::sort(out.begin(), out.end(), [](const MilkPreset& a, const MilkPreset& b) {
            return milkLower(pathUtf8(a.path)) < milkLower(pathUtf8(b.path));
        });
        return out;
    }

    void rebuild(const std::filesystem::path& bundledDir, const std::filesystem::path& userDir) {
        presets = scan(bundledDir, false, true, 1000, nullptr);
        bundledCount = (int)presets.size();
        userTruncated = false;
        auto user = scan(userDir, true, false, kUserLimit, &userTruncated);
        presets.insert(presets.end(), user.begin(), user.end());
    }

    int userCount() const { return (int)presets.size() - bundledCount; }

    std::vector<std::string> names() const {
        std::vector<std::string> n;
        n.reserve(presets.size());
        for (const auto& p : presets) n.push_back(p.name);
        return n;
    }
};

inline std::string readMilkFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/// Folders projectM searches for the textures a preset names: a "Textures" folder beside or above
/// the presets (the NestDrop and MilkDrop layout), the preset's own folder, and the bundled ones.
inline std::vector<std::string> milkTexturePaths(const std::filesystem::path& presetFile,
                                                 const std::filesystem::path& userDir,
                                                 const std::filesystem::path& bundledTextures) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    auto add = [&](const fs::path& p) {
        if (!p.empty() && fs::is_directory(p, ec)) out.push_back(pathUtf8(p));
    };
    add(presetFile.parent_path());
    for (fs::path p = presetFile.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
        add(p / "Textures");
        add(p / "textures");
        if (!userDir.empty() && p == userDir) {
            add(p.parent_path() / "Textures");
            break;
        }
    }
    add(bundledTextures);
    return out;
}

/// Writes the presets and textures built into the app (milkdrop_bundled.h) to `dir`, skipping
/// files that are already there with the same size.
template <typename File, size_t N>
inline void extractBundledMilk(const File (&files)[N], const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    for (const File& f : files) {
        fs::path target = dir / fs::u8path(f.path);
        std::error_code ec;
        if (fs::exists(target, ec) && fs::file_size(target, ec) == f.size) continue;
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (out) out.write(reinterpret_cast<const char*>(f.data), (std::streamsize)f.size);
    }
}

