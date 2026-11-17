// Dropocalypse for Linux - logo image loading (stb_image) and the "YOUR LOGO" placeholder
// (stb_truetype with a system font).

#include "renderer.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr int kMaxLogoSize = 1024;

/// Crops fully transparent borders, then pads by 6% so the mip-blurred glow has room to spread.
static LogoImage trimAndPad(const std::vector<uint8_t>& px, int w, int h) {
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = px.data() + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            if (row[x * 4 + 3] > 8) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < minX) { minX = 0; minY = 0; maxX = w - 1; maxY = h - 1; } // fully transparent: keep as is

    int cw = maxX - minX + 1, ch = maxY - minY + 1;
    int pad = (int)(std::max(cw, ch) * 0.06f);
    LogoImage out;
    out.width = cw + pad * 2;
    out.height = ch + pad * 2;
    out.rgba.assign((size_t)out.width * out.height * 4, 0);
    for (int y = 0; y < ch; ++y) {
        std::memcpy(&out.rgba[((size_t)(y + pad) * out.width + pad) * 4],
                    &px[((size_t)(y + minY) * w + minX) * 4], (size_t)cw * 4);
    }
    return out;
}

bool loadLogoImage(const std::wstring& path, LogoImage& out, std::wstring& error) {
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(narrow(path).c_str(), &w, &h, &n, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        error = L"Couldn't open that file as an image. Try a PNG, JPG, GIF, BMP or TGA.";
        return false;
    }
    std::vector<uint8_t> px(data, data + (size_t)w * h * 4);
    stbi_image_free(data);

    // Premultiply alpha (the shaders expect premultiplied colors).
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        unsigned a = px[i * 4 + 3];
        for (int c = 0; c < 3; ++c) px[i * 4 + c] = (uint8_t)((px[i * 4 + c] * a + 127) / 255);
    }

    // Scale down large images (box filter on premultiplied pixels, so edges don't darken).
    double scale = std::min(1.0, (double)kMaxLogoSize / std::max(w, h));
    if (scale < 1.0) {
        int nw = std::max(1, (int)(w * scale)), nh = std::max(1, (int)(h * scale));
        std::vector<uint8_t> small((size_t)nw * nh * 4);
        for (int y = 0; y < nh; ++y) {
            int y0 = (int)((long long)y * h / nh), y1 = std::max(y0 + 1, (int)((long long)(y + 1) * h / nh));
            for (int x = 0; x < nw; ++x) {
                int x0 = (int)((long long)x * w / nw), x1 = std::max(x0 + 1, (int)((long long)(x + 1) * w / nw));
                unsigned sum[4] = {0, 0, 0, 0};
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx)
                        for (int c = 0; c < 4; ++c) sum[c] += px[((size_t)yy * w + xx) * 4 + c];
                unsigned n = (unsigned)((x1 - x0) * (y1 - y0));
                for (int c = 0; c < 4; ++c) small[((size_t)y * nw + x) * 4 + c] = (uint8_t)((sum[c] + n / 2) / n);
            }
        }
        px.swap(small);
        w = nw;
        h = nh;
    }
    out = trimAndPad(px, w, h);
    return true;
}

static bool readFile(const char* path, std::vector<unsigned char>& data) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) { std::fclose(f); return false; }
    data.resize((size_t)size);
    bool ok = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

/// First readable bold sans font from the usual places on Linux distributions.
static bool loadFont(std::vector<unsigned char>& data, bool bold) {
    const char* boldFonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",          // Debian, Ubuntu
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",        // Fedora
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",                      // Arch
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
        "/usr/share/fonts/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
    };
    const char* regularFonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
    };
    for (const char* p : bold ? boldFonts : regularFonts)
        if (readFile(p, data)) return true;
    if (!bold) return loadFont(data, true);
    return false;
}

/// Draws `text` centered horizontally with its baseline at `baseline`, as white coverage.
static void drawText(std::vector<uint8_t>& cov, int w, int h, const std::vector<unsigned char>& font,
                     const char* text, float pixelHeight, int baseline) {
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, font.data(), stbtt_GetFontOffsetForIndex(font.data(), 0))) return;
    float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    float width = 0;
    for (const char* c = text; *c; ++c) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&info, *c, &adv, &lsb);
        width += adv * scale;
        if (c[1]) width += stbtt_GetCodepointKernAdvance(&info, c[0], c[1]) * scale;
    }
    float x = (w - width) * 0.5f;
    for (const char* c = text; *c; ++c) {
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&info, *c, &adv, &lsb);
        stbtt_GetCodepointBitmapBox(&info, *c, scale, scale, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            std::vector<unsigned char> glyph((size_t)gw * gh);
            stbtt_MakeCodepointBitmap(&info, glyph.data(), gw, gh, gw, scale, scale, *c);
            int ox = (int)x + x0, oy = baseline + y0;
            for (int yy = 0; yy < gh; ++yy)
                for (int xx = 0; xx < gw; ++xx) {
                    int px = ox + xx, py = oy + yy;
                    if (px < 0 || py < 0 || px >= w || py >= h) continue;
                    uint8_t& d = cov[(size_t)py * w + px];
                    d = std::max(d, glyph[(size_t)yy * gw + xx]);
                }
        }
        x += adv * scale;
        if (c[1]) x += stbtt_GetCodepointKernAdvance(&info, c[0], c[1]) * scale;
    }
}

LogoImage placeholderLogo() {
    const int w = 1024, h = 400;
    LogoImage out;
    out.width = w;
    out.height = h;
    out.rgba.assign((size_t)w * h * 4, 0);
    std::vector<uint8_t> cov((size_t)w * h, 0);
    std::vector<unsigned char> bold, regular;
    if (loadFont(bold, true)) drawText(cov, w, h, bold, "YOUR LOGO", 170, 215);
    if (loadFont(regular, false)) drawText(cov, w, h, regular, "drop an image here or press L", 46, 325);
    if (bold.empty()) {
        // No font found: a simple white bar so there's still something to bounce.
        for (int y = 160; y < 240; ++y)
            for (int x = 192; x < 832; ++x) cov[(size_t)y * w + x] = 255;
    }
    // White coverage -> premultiplied white with alpha = coverage.
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        uint8_t v = cov[i];
        out.rgba[i * 4 + 0] = v;
        out.rgba[i * 4 + 1] = v;
        out.rgba[i * 4 + 2] = v;
        out.rgba[i * 4 + 3] = v;
    }
    return out;
}
