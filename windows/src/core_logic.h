// Pure update and auto-cycle decisions shared by the app and the unit tests.
// No Windows headers: the test binary compiles this on its own.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

/// Dotted versions compared numerically (0.10.0 > 0.9.2). Anything after "-" is ignored.
/// Equal versions are not newer. This is the same rule the Mac app uses.
inline std::vector<int> versionComponents(const std::string& s) {
    std::vector<int> v;
    std::string core = s.substr(0, s.find('-'));
    size_t i = 0;
    while (i <= core.size()) {
        size_t j = core.find('.', i);
        if (j == std::string::npos) j = core.size();
        v.push_back(std::atoi(core.substr(i, j - i).c_str()));
        i = j + 1;
    }
    return v;
}

inline bool versionIsNewer(const std::string& a, const std::string& b) {
    const std::vector<int> x = versionComponents(a);
    const std::vector<int> y = versionComponents(b);
    const size_t n = x.size() > y.size() ? x.size() : y.size();
    for (size_t i = 0; i < n; ++i) {
        const int p = i < x.size() ? x[i] : 0;
        const int q = i < y.size() ? y[i] : 0;
        if (p != q) return p > q;
    }
    return false;
}

/// "<hash>  <file>" or "<hash> *<file>" from a sha256sum listing. Empty if `file` is absent.
inline std::string checksumFor(const std::string& file, const std::string& listing) {
    size_t i = 0;
    while (i < listing.size()) {
        size_t end = listing.find_first_of("\r\n", i);
        if (end == std::string::npos) end = listing.size();
        const std::string line = listing.substr(i, end - i);
        i = end;
        if (i < listing.size() && listing[i] == '\r') ++i;
        if (i < listing.size() && listing[i] == '\n') ++i;

        std::vector<std::string> parts;
        std::string cur;
        for (char c : line) {
            if (c == ' ') {
                if (!cur.empty()) {
                    parts.push_back(cur);
                    cur.clear();
                }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) parts.push_back(cur);
        if (parts.size() < 2) continue;
        std::string name = parts.back();
        if (!name.empty() && name[0] == '*') name.erase(name.begin());
        if (name == file) return parts.front();
    }
    return {};
}

/// Bit i set in `cycleSkip` means style i is left out of auto-cycle.
inline bool cycleBitSkipped(std::uint32_t cycleSkip, int index) {
    return (cycleSkip & (1u << index)) != 0;
}

/// Next style after `index`. If every style is skipped, still advances by one
/// (auto-cycle keeps moving when nothing is checked).
inline int nextCycleIndex(int index, int count, std::uint32_t cycleSkip) {
    if (count <= 0) return 0;
    const int n = count;
    int requested = index % n;
    if (requested < 0) requested += n;
    int next = (requested + 1) % n;
    for (int k = 1; k <= n; ++k) {
        const int c = (requested + k) % n;
        if (!cycleBitSkipped(cycleSkip, c)) {
            next = c;
            break;
        }
    }
    return next;
}

/// Signature facts for one update. The app fills these in from WinVerifyTrust;
/// the decision itself does not call Windows.
struct UpdateSignatureFacts {
    bool runningIsSigned = false;
    std::string runningPublisher;
    bool updateIsSigned = false;
    bool updateTrusted = false;
    std::string updatePublisher;
};

/// nullptr means the update may be installed.
/// An unsigned running copy does not require a signature, so today's unsigned builds still update.
/// Once the running exe has a trusted Authenticode signature, the download must be trusted and
/// signed by the same publisher.
inline const char* updateSignatureRefusal(const UpdateSignatureFacts& f) {
    if (!f.runningIsSigned) return nullptr;
    if (!f.updateIsSigned || !f.updateTrusted)
        return "The downloaded app isn't signed with a trusted certificate, so it wasn't installed.";
    if (f.runningPublisher.empty() || f.updatePublisher.empty())
        return "Couldn't confirm the update is signed by the same publisher, so it wasn't installed.";
    if (f.updatePublisher != f.runningPublisher)
        return "The downloaded app is signed by a different publisher, so it wasn't installed.";
    return nullptr;
}
