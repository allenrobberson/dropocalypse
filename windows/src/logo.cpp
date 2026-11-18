// Dropocalypse for Windows - logo image loading (WIC) and the "YOUR LOGO" placeholder (GDI).

#include "renderer.h"

#include <wincodec.h>

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

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
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                &decoder);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0) {
        error = L"Couldn't open that file as an image. Try a PNG, JPG, GIF, BMP or TIFF.";
        return false;
    }

    // Scale down large images first.
    ComPtr<IWICBitmapSource> source = frame;
    double scale = std::min(1.0, (double)kMaxLogoSize / std::max(w, h));
    if (scale < 1.0) {
        UINT nw = std::max<UINT>(1, (UINT)(w * scale)), nh = std::max<UINT>(1, (UINT)(h * scale));
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame.Get(), nw, nh, WICBitmapInterpolationModeFant))) {
            source = scaler;
            w = nw;
            h = nh;
        }
    }

    ComPtr<IWICFormatConverter> conv;
    hr = factory->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr))
        hr = conv->Initialize(source.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom);
    std::vector<uint8_t> px((size_t)w * h * 4);
    if (SUCCEEDED(hr)) hr = conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data());
    if (FAILED(hr)) {
        error = L"Couldn't decode that image.";
        return false;
    }

    out = trimAndPad(px, (int)w, (int)h);
    return true;
}

LogoImage placeholderLogo() {
    const int w = 1024, h = 400;
    LogoImage out;
    out.width = w;
    out.height = h;
    out.rgba.assign((size_t)w * h * 4, 0);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return out;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    std::memset(bits, 0, (size_t)w * h * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, 0x00FFFFFF);

    HFONT bigFont = CreateFontW(-150, 0, 0, 0, FW_BLACK, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI Black");
    HFONT smallFont = CreateFontW(-44, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, bigFont);
    RECT r1{0, 40, w, 260};
    DrawTextW(dc, L"YOUR LOGO", -1, &r1, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, smallFont);
    RECT r2{0, 270, w, 350};
    DrawTextW(dc, L"drop an image here or press L", -1, &r2, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    GdiFlush();

    // White text on black -> premultiplied white with alpha = brightness.
    const uint8_t* src = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        uint8_t v = std::max({src[i * 4 + 0], src[i * 4 + 1], src[i * 4 + 2]});
        out.rgba[i * 4 + 0] = v;
        out.rgba[i * 4 + 1] = v;
        out.rgba[i * 4 + 2] = v;
        out.rgba[i * 4 + 3] = v;
    }

    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bigFont);
    DeleteObject(smallFont);
    DeleteObject(bmp);
    DeleteDC(dc);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Text items
// ---------------------------------------------------------------------------------------------

LogoImage renderTextLogo(const std::string& utf8Text, int font, int color) {
    LogoImage out;
    std::string clean = cleanItemText(utf8Text);
    if (clean.empty()) return out;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, clean.c_str(), (int)clean.size(), nullptr, 0);
    if (wlen <= 0) return out;
    std::wstring text((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, clean.c_str(), (int)clean.size(), &text[0], wlen);

    struct Face { const wchar_t* name; int weight; };
    static const Face faces[kTextFontCount] = {
        {L"Segoe UI", FW_BOLD}, {L"Segoe UI Black", FW_BLACK}, {L"Georgia", FW_BOLD}, {L"Consolas", FW_BOLD}};
    const Face& face = faces[(font >= 0 && font < kTextFontCount) ? font : 0];

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return out;
    int pixelHeight = 200;
    HFONT hfont = nullptr;
    SIZE size{};
    for (int attempt = 0; attempt < 6; ++attempt) {   // shrink very long text so it stays under 2400 px
        if (hfont) DeleteObject(hfont);
        hfont = CreateFontW(-pixelHeight, 0, 0, 0, face.weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face.name);
        SelectObject(dc, hfont);
        RECT r{0, 0, 0, 0};
        DrawTextW(dc, text.c_str(), (int)text.size(), &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        size.cx = r.right - r.left;
        size.cy = r.bottom - r.top;
        if (size.cx <= 2400 || pixelHeight <= 60) break;
        pixelHeight = std::max(60, pixelHeight * 2400 / std::max<LONG>(1, size.cx));
    }
    const int outline = std::max(4, pixelHeight / 18);
    const int margin = outline + 4;
    const int w = std::max<LONG>(1, size.cx) + margin * 2, h = std::max<LONG>(1, size.cy) + margin * 2;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteObject(hfont);
        DeleteDC(dc);
        return out;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    SelectObject(dc, hfont);
    std::memset(bits, 0, (size_t)w * h * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, 0x00FFFFFF);
    RECT r{margin, margin, w - margin, h - margin};
    DrawTextW(dc, text.c_str(), (int)text.size(), &r, DT_SINGLELINE | DT_NOPREFIX | DT_LEFT | DT_TOP);
    GdiFlush();

    std::vector<uint8_t> cov((size_t)w * h);
    const uint8_t* src = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < cov.size(); ++i) cov[i] = std::max({src[i * 4 + 0], src[i * 4 + 1], src[i * 4 + 2]});
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteObject(hfont);
    DeleteDC(dc);

    std::vector<uint8_t> px = textCoverageToRGBA(cov, w, h, textColorRGB(color), outline);
    if (px.empty()) return out;
    return trimAndPad(px, w, h);
}
