// Tiny JSON helpers for the phone remote's messages (Windows and Linux; the Mac uses Foundation).
// Messages from the phone are flat objects: {"t":"set","id":"motion.pulse","v":0.5}.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

struct FlatJson {
    std::map<std::string, std::string> str;   // string values
    std::map<std::string, double> num;        // numbers, and booleans as 0 / 1
    bool has(const std::string& k) const { return str.count(k) || num.count(k); }
    std::string s(const std::string& k) const { auto it = str.find(k); return it == str.end() ? "" : it->second; }
    double n(const std::string& k, double fallback = 0) const { auto it = num.find(k); return it == num.end() ? fallback : it->second; }
};

/// Parses a flat JSON object. Nested objects and arrays are skipped. Returns false on bad input.
inline bool parseFlatJson(const std::string& j, FlatJson& out) {
    size_t i = 0;
    auto ws = [&] { while (i < j.size() && (j[i] == ' ' || j[i] == '\t' || j[i] == '\n' || j[i] == '\r')) ++i; };
    auto str = [&](std::string& s) -> bool {
        if (i >= j.size() || j[i] != '"') return false;
        ++i;
        while (i < j.size() && j[i] != '"') {
            char c = j[i++];
            if (c == '\\' && i < j.size()) {
                char e = j[i++];
                if (e == 'n') s += '\n';
                else if (e == 't') s += '\t';
                else if (e == 'r') s += '\r';
                else if (e == 'b' || e == 'f') {}
                else if (e == 'u' && i + 4 <= j.size()) {
                    unsigned cp = (unsigned)std::strtoul(j.substr(i, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= j.size() && j[i] == '\\' && j[i + 1] == 'u') {
                        unsigned lo = (unsigned)std::strtoul(j.substr(i + 2, 4).c_str(), nullptr, 16);
                        i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    if (cp < 0x80) s += (char)cp;
                    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
                    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 63)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
                } else s += e;
            } else s += c;
        }
        if (i >= j.size()) return false;
        ++i;
        return true;
    };
    auto skip = [&]() -> bool {   // a nested value we don't need
        int depth = 0;
        do {
            if (i >= j.size()) return false;
            if (j[i] == '"') { std::string t; if (!str(t)) return false; continue; }
            if (j[i] == '{' || j[i] == '[') ++depth;
            if (j[i] == '}' || j[i] == ']') --depth;
            ++i;
        } while (depth > 0);
        return true;
    };
    ws();
    if (i >= j.size() || j[i] != '{') return false;
    ++i;
    for (;;) {
        ws();
        if (i < j.size() && j[i] == '}') return true;
        std::string key;
        if (!str(key)) return false;
        ws();
        if (i >= j.size() || j[i] != ':') return false;
        ++i;
        ws();
        if (i >= j.size()) return false;
        const char c = j[i];
        if (c == '"') { std::string v; if (!str(v)) return false; out.str[key] = v; }
        else if (c == '{' || c == '[') { if (!skip()) return false; }
        else if (j.compare(i, 4, "true") == 0) { out.num[key] = 1; i += 4; }
        else if (j.compare(i, 5, "false") == 0) { out.num[key] = 0; i += 5; }
        else if (j.compare(i, 4, "null") == 0) { i += 4; }
        else {
            char* end = nullptr;
            double v = std::strtod(j.c_str() + i, &end);
            if (end == j.c_str() + i) return false;
            out.num[key] = v;
            i = (size_t)(end - j.c_str());
        }
        ws();
        if (i < j.size() && j[i] == ',') { ++i; continue; }
        if (i < j.size() && j[i] == '}') return true;
        return false;
    }
}

inline std::string jsonQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += c;
    }
    return out + "\"";
}

inline std::string jsonNumber(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.4f", v);
    std::string s = b;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}
