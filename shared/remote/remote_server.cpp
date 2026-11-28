// Phone remote web server (see include/remote_server.h). Plain sockets, one thread, no dependencies
// beyond the operating system. Serves the phone page, upgrades /ws to a WebSocket, checks the
// pairing code / token, accepts logo uploads and only talks to devices on the local network.

#include "remote_server.h"

#include "qrcodegen.h"
#include "remote_assets.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
static const sock_t kBadSock = INVALID_SOCKET;
static void closeSock(sock_t s) { closesocket(s); }
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
static const sock_t kBadSock = -1;
static void closeSock(sock_t s) { close(s); }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

const size_t kMaxHeader = 16 * 1024;
const size_t kMaxMessage = 256 * 1024;
const size_t kMaxUpload = 30 * 1024 * 1024;
const int kMaxClients = 16;

std::string gLastError;

// ---- SHA-1 and base64, for the WebSocket handshake.
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    static uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }
    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::string digest(const std::string& msg) {
        std::string m = msg;
        uint64_t bits = (uint64_t)msg.size() * 8;
        m.push_back((char)0x80);
        while (m.size() % 64 != 56) m.push_back(0);
        for (int i = 7; i >= 0; --i) m.push_back((char)(bits >> (i * 8)));
        for (size_t i = 0; i < m.size(); i += 64) block((const uint8_t*)m.data() + i);
        std::string out;
        for (uint32_t v : h) for (int i = 3; i >= 0; --i) out.push_back((char)(v >> (i * 8)));
        return out;
    }
};

std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8;
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += '=';
    }
    return out;
}

std::string randomString(size_t n, const char* alphabet) {
    static std::mt19937_64 rng = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd(),
                          (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count()};
        return std::mt19937_64(seq);
    }();
    const size_t len = std::strlen(alphabet);
    std::string out;
    for (size_t i = 0; i < n; ++i) out += alphabet[rng() % len];
    return out;
}

std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

/// Value of a query parameter ("" if missing), with %XX and + decoded.
std::string queryParam(const std::string& path, const std::string& key) {
    size_t q = path.find('?');
    if (q == std::string::npos) return "";
    std::string qs = path.substr(q + 1);
    size_t pos = 0;
    while (pos <= qs.size()) {
        size_t amp = qs.find('&', pos);
        if (amp == std::string::npos) amp = qs.size();
        std::string part = qs.substr(pos, amp - pos);
        size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == key) {
            std::string v = part.substr(eq + 1), out;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == '+') out += ' ';
                else if (v[i] == '%' && i + 2 < v.size()) {
                    out += (char)std::strtol(v.substr(i + 1, 2).c_str(), nullptr, 16);
                    i += 2;
                } else out += v[i];
            }
            return out;
        }
        pos = amp + 1;
    }
    return "";
}

std::string header(const std::string& head, const char* name) {
    const std::string lh = lower(head), key = "\r\n" + lower(name) + ":";
    size_t p = lh.find(key);
    if (p == std::string::npos) return "";
    p += key.size();
    size_t e = head.find("\r\n", p);
    std::string v = head.substr(p, e - p);
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
    return v;
}

/// Private, loopback and link-local IPv4 addresses: the only peers the remote talks to.
bool isLocalNetwork(uint32_t ip /* host order */) {
    const uint8_t a = ip >> 24, b = (ip >> 16) & 255;
    return a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
           (a == 169 && b == 254) || (a == 100 && b >= 64 && b <= 127) /* carrier-grade NAT, some hotspots */;
}

std::string ipString(uint32_t ip) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return buf;
}

/// Every private IPv4 address of this computer, with the network's name where the system gives one:
/// {"192.168.1.20", "en0"}.
std::vector<std::pair<std::string, std::string>> listLocalIps() {
    std::vector<std::pair<std::string, std::string>> out;
    auto add = [&](uint32_t ip, const std::string& name) {
        if (!isLocalNetwork(ip) || (ip >> 24) == 127) return;
        for (auto& e : out) if (e.first == ipString(ip)) return;
        out.emplace_back(ipString(ip), name);
    };
#ifdef _WIN32
    char host[256];
    if (gethostname(host, sizeof(host)) == 0) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        if (getaddrinfo(host, nullptr, &hints, &res) == 0) {
            for (addrinfo* p = res; p; p = p->ai_next) add(ntohl(((sockaddr_in*)p->ai_addr)->sin_addr.s_addr), "");
            freeaddrinfo(res);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* p = list; p; p = p->ifa_next)
            if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET)
                add(ntohl(((sockaddr_in*)p->ifa_addr)->sin_addr.s_addr), p->ifa_name ? p->ifa_name : "");
        freeifaddrs(list);
    }
#endif
    return out;
}

/// The address to put in the QR code: the one the user picked (while this computer still has it),
/// else the one the default route uses, else the first private address.
std::string findLocalIp(const std::string& preferred) {
    // For testing, and computers whose networks the app can't see: DROPOCALYPSE_REMOTE_ADDRESS=192.168.1.20
    if (const char* forced = std::getenv("DROPOCALYPSE_REMOTE_ADDRESS"))
        if (*forced) return forced;
    const auto all = listLocalIps();
    for (auto& e : all) if (e.first == preferred) return preferred;
    sock_t u = socket(AF_INET, SOCK_DGRAM, 0);
    if (u != kBadSock) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);   // nothing is sent: connect() only picks a route
        uint32_t ip = 0;
        if (connect(u, (sockaddr*)&to, sizeof(to)) == 0) {
            sockaddr_in me{};
            socklen_t len = sizeof(me);
            if (getsockname(u, (sockaddr*)&me, &len) == 0) ip = ntohl(me.sin_addr.s_addr);
        }
        closeSock(u);
        if (ip && isLocalNetwork(ip) && (ip >> 24) != 127) return ipString(ip);
    }
    return all.empty() ? "" : all.front().first;
}

std::string deviceName(const std::string& ua) {
    if (ua.find("iPhone") != std::string::npos) return "iPhone";
    if (ua.find("iPad") != std::string::npos) return "iPad";
    if (ua.find("Android") != std::string::npos) return ua.find("Mobile") != std::string::npos ? "Android phone" : "Android tablet";
    if (ua.find("Macintosh") != std::string::npos) return "Mac browser";
    if (ua.find("Windows") != std::string::npos) return "Windows browser";
    if (ua.find("Linux") != std::string::npos) return "Linux browser";
    return "Browser";
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += c;
    }
    return out;
}

struct Client {
    sock_t fd = kBadSock;
    int id = 0;
    uint32_t ip = 0;
    std::string in, out;
    bool ws = false;            // upgraded to a WebSocket and paired
    bool closing = false;       // close once `out` has been sent
    std::string label;
    std::string fragment;       // a WebSocket message arriving in pieces
    std::chrono::steady_clock::time_point lastSeen = std::chrono::steady_clock::now();
};

struct Event {
    int type, client;
    std::string data, name;
};

} // namespace

struct RemoteServer {
    sock_t listener = kBadSock;
    int port = 0;
    std::string platform;
    std::thread thread;
    std::atomic<bool> quit{false};

    std::mutex m;                 // guards everything below
    std::vector<std::unique_ptr<Client>> clients;
    std::deque<Event> events;
    std::vector<std::string> tokens;   // paired phones, oldest first
    std::string preferredIp;
    std::string pairCode;
    std::string localIp;
    std::chrono::steady_clock::time_point ipCheckedAt{};
    int nextId = 1;
    bool dropAll = false;

    // Returned to the caller (valid until the next call).
    Event current;
    std::string urlOut, clientsOut, ipOut, codeOut, tokensOut, addressesOut;

    void newPairCode() { pairCode = randomString(8, "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"); }

    void wsSend(Client& c, const std::string& payload, uint8_t opcode = 1) {
        std::string f;
        f += (char)(0x80 | opcode);
        const size_t n = payload.size();
        if (n < 126) f += (char)n;
        else if (n < 65536) { f += (char)126; f += (char)(n >> 8); f += (char)(n & 255); }
        else { f += (char)127; for (int i = 7; i >= 0; --i) f += (char)((uint64_t)n >> (i * 8)); }
        f += payload;
        c.out += f;
    }

    void httpReply(Client& c, const char* status, const char* type, const std::string& body) {
        std::string r = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type +
                        "\r\nContent-Length: " + std::to_string(body.size()) +
                        "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                        "Referrer-Policy: no-referrer\r\nConnection: close\r\n\r\n" + body;
        c.out += r;
        c.closing = true;
    }

    bool tokenOk(const std::string& t) {
        if (t.empty()) return false;
        for (auto& k : tokens) if (k == t) return true;
        return false;
    }

    /// A complete HTTP request header is in c.in: answer it. Returns false if more bytes are needed
    /// (an upload body still arriving).
    bool handleHttp(Client& c, size_t headEnd) {
        const std::string head = c.in.substr(0, headEnd);
        const size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) { httpReply(c, "400 Bad Request", "text/plain", "Bad request"); return true; }
        const std::string method = head.substr(0, sp1), path = head.substr(sp1 + 1, sp2 - sp1 - 1);
        const std::string route = path.substr(0, path.find('?'));

        if (method == "GET" && (route == "/" || route == "/index.html")) {
            c.in.erase(0, headEnd + 4);
            httpReply(c, "200 OK", "text/html; charset=utf-8", std::string(kRemotePage, sizeof(kRemotePage) - 1));
            return true;
        }
        if (method == "GET" && route == "/ws") {
            c.in.erase(0, headEnd + 4);
            const std::string key = header(head, "Sec-WebSocket-Key");
            if (lower(header(head, "Upgrade")) != "websocket" || key.empty()) {
                httpReply(c, "400 Bad Request", "text/plain", "WebSocket only");
                return true;
            }
            // Only pages served by this app may connect (stops other web sites from trying).
            const std::string origin = header(head, "Origin"), host = header(head, "Host");
            if (!origin.empty() && origin != "http://" + host) {
                httpReply(c, "403 Forbidden", "text/plain", "Wrong origin");
                return true;
            }
            std::string token = queryParam(path, "token");
            const std::string pair = queryParam(path, "pair");
            if (!tokenOk(token)) {
                if (pair.empty() || pair != pairCode) {
                    httpReply(c, "403 Forbidden", "text/plain", "Pairing code expired");
                    return true;
                }
                token = randomString(32, "abcdefghijklmnopqrstuvwxyz0123456789");
                tokens.push_back(token);
                if (tokens.size() > 16) tokens.erase(tokens.begin());   // forget the oldest phone
                newPairCode();   // one-time: the next phone needs the new code
            }
            Sha1 sha;
            const std::string accept = base64(sha.digest(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
            c.out += "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
            c.ws = true;
            c.label = deviceName(header(head, "User-Agent")) + " (" + ipString(c.ip) + ")";
            wsSend(c, "{\"t\":\"hello\",\"token\":\"" + token + "\",\"platform\":\"" + jsonEscape(platform) +
                          "\",\"schema\":" + std::string(kRemoteSchema, sizeof(kRemoteSchema) - 1) + "}");
            events.push_back({REMOTE_EVENT_CONNECTED, c.id, "", ""});
            return true;
        }
        if (method == "POST" && route == "/upload") {
            if (!tokenOk(queryParam(path, "token"))) {
                httpReply(c, "403 Forbidden", "text/plain", "Not paired");
                return true;
            }
            const long long len = std::atoll(header(head, "Content-Length").c_str());
            if (len <= 0 || (size_t)len > kMaxUpload) {
                httpReply(c, "413 Payload Too Large", "text/plain", "That file is too big (30 MB at most).");
                return true;
            }
            if (c.in.size() < headEnd + 4 + (size_t)len) return false;   // still arriving
            std::string name = queryParam(path, "name");
            if (name.size() > 200) name.resize(200);
            events.push_back({REMOTE_EVENT_UPLOAD, c.id, c.in.substr(headEnd + 4, (size_t)len), name});
            c.in.clear();
            httpReply(c, "200 OK", "application/json", "{\"ok\":true}");
            return true;
        }
        c.in.clear();
        httpReply(c, "404 Not Found", "text/plain", "Not found");
        return true;
    }

    /// Reads WebSocket frames from c.in.
    void handleFrames(Client& c) {
        for (;;) {
            if (c.in.size() < 2) return;
            const uint8_t b0 = (uint8_t)c.in[0], b1 = (uint8_t)c.in[1];
            const int opcode = b0 & 15;
            const bool fin = (b0 & 0x80) != 0, masked = (b1 & 0x80) != 0;
            uint64_t len = b1 & 127;
            size_t pos = 2;
            if (len == 126) {
                if (c.in.size() < 4) return;
                len = (uint64_t)(uint8_t)c.in[2] << 8 | (uint8_t)c.in[3];
                pos = 4;
            } else if (len == 127) {
                if (c.in.size() < 10) return;
                len = 0;
                for (int i = 0; i < 8; ++i) len = len << 8 | (uint8_t)c.in[2 + i];
                pos = 10;
            }
            if (!masked || len > kMaxMessage) { c.closing = true; c.in.clear(); return; }
            if (c.in.size() < pos + 4 + len) return;
            const uint8_t* mask = (const uint8_t*)c.in.data() + pos;
            std::string payload = c.in.substr(pos + 4, (size_t)len);
            for (size_t i = 0; i < payload.size(); ++i) payload[i] = (char)(payload[i] ^ mask[i & 3]);
            c.in.erase(0, pos + 4 + (size_t)len);

            if (opcode == 8) { wsSend(c, "", 8); c.closing = true; return; }
            if (opcode == 9) { wsSend(c, payload, 10); continue; }
            if (opcode == 10) continue;
            if (opcode == 1 || opcode == 2 || opcode == 0) {
                c.fragment += payload;
                if (c.fragment.size() > kMaxMessage) { c.closing = true; return; }
                if (fin) {
                    events.push_back({REMOTE_EVENT_MESSAGE, c.id, c.fragment, ""});
                    c.fragment.clear();
                }
            }
        }
    }

    void dropClient(size_t i) {
        Client& c = *clients[i];
        if (c.ws) events.push_back({REMOTE_EVENT_DISCONNECTED, c.id, "", ""});
        closeSock(c.fd);
        clients.erase(clients.begin() + (long)i);
    }

    void run() {
        using clock = std::chrono::steady_clock;
        auto lastPing = clock::now();
        while (!quit) {
            fd_set rd, wr;
            FD_ZERO(&rd);
            FD_ZERO(&wr);
            FD_SET(listener, &rd);
            sock_t maxFd = listener;
            {
                std::lock_guard<std::mutex> lock(m);
                if (dropAll) {
                    dropAll = false;
                    for (auto& c : clients) if (c->ws) { wsSend(*c, "", 8); c->closing = true; }
                }
                for (auto& c : clients) {
                    FD_SET(c->fd, &rd);
                    if (!c->out.empty()) FD_SET(c->fd, &wr);
                    if (c->fd > maxFd) maxFd = c->fd;
                }
            }
            timeval tv{0, 50 * 1000};
            int n = select((int)maxFd + 1, &rd, &wr, nullptr, &tv);
            if (quit) break;
            if (n < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }

            std::lock_guard<std::mutex> lock(m);
            if (FD_ISSET(listener, &rd)) {
                sockaddr_in peer{};
                socklen_t len = sizeof(peer);
                sock_t fd = accept(listener, (sockaddr*)&peer, &len);
                if (fd != kBadSock) {
                    const uint32_t ip = ntohl(peer.sin_addr.s_addr);
                    if (!isLocalNetwork(ip) || (int)clients.size() >= kMaxClients) {
                        closeSock(fd);   // LAN only
                    } else {
#ifdef _WIN32
                        u_long nb = 1;
                        ioctlsocket(fd, FIONBIO, &nb);
#else
                        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
                        int one = 1;
                        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
#endif
                        int nodelay = 1;
                        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
                        auto c = std::make_unique<Client>();
                        c->fd = fd;
                        c->ip = ip;
                        c->id = nextId++;
                        clients.push_back(std::move(c));
                    }
                }
            }
            const auto now = clock::now();
            const bool ping = now - lastPing > std::chrono::seconds(15);
            if (ping) lastPing = now;
            for (size_t i = 0; i < clients.size();) {
                Client& c = *clients[i];
                bool dead = false;
                if (FD_ISSET(c.fd, &rd)) {
                    char buf[65536];
                    int got = (int)recv(c.fd, buf, sizeof(buf), 0);
                    if (got > 0) {
                        c.lastSeen = now;
                        if (!c.closing) c.in.append(buf, (size_t)got);
                        if (!c.ws) {
                            size_t he = c.in.find("\r\n\r\n");
                            if (he != std::string::npos) {
                                if (!handleHttp(c, he) && c.in.size() > kMaxUpload + kMaxHeader) dead = true;
                            } else if (c.in.size() > kMaxHeader) dead = true;
                        }
                        if (c.ws) handleFrames(c);   // also frames that came in with the upgrade request
                    } else if (got == 0 || !wouldBlock()) {
                        dead = true;
                    }
                }
                if (!dead && !c.out.empty() && FD_ISSET(c.fd, &wr)) {
                    int sent = (int)send(c.fd, c.out.data(), (int)std::min<size_t>(c.out.size(), 1 << 20), MSG_NOSIGNAL);
                    if (sent > 0) c.out.erase(0, (size_t)sent);
                    else if (sent < 0 && !wouldBlock()) dead = true;
                }
                if (c.closing && c.out.empty()) dead = true;
                if (c.ws && ping && !dead) wsSend(c, "", 9);
                // Phones that went to sleep without saying goodbye; idle half-open HTTP requests.
                const auto idle = now - c.lastSeen;
                if (idle > std::chrono::seconds(c.ws ? 45 : 60)) dead = true;
                if (dead) dropClient(i);
                else ++i;
            }
        }
    }
};

extern "C" {

RemoteServer* remote_start(int preferredPort, const char* platform) {
#ifdef _WIN32
    static bool wsaReady = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    if (!wsaReady) { gLastError = "Couldn't start networking."; return nullptr; }
#endif
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kBadSock) { gLastError = "Couldn't open a network socket."; return nullptr; }
#ifdef _WIN32
    BOOL excl = TRUE;
    setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
#else
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    int port = 0;
    for (int attempt = 0; attempt <= 20; ++attempt) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((uint16_t)(attempt < 20 ? preferredPort + attempt : 0));
        if (bind(fd, (sockaddr*)&a, sizeof(a)) == 0) {
            sockaddr_in got{};
            socklen_t len = sizeof(got);
            getsockname(fd, (sockaddr*)&got, &len);
            port = ntohs(got.sin_port);
            break;
        }
    }
    if (port == 0 || listen(fd, 16) != 0) {
        closeSock(fd);
        gLastError = "Couldn't start the phone remote (no free network port).";
        return nullptr;
    }
    auto* s = new RemoteServer();
    s->listener = fd;
    s->port = port;
    s->platform = platform ? platform : "";
    s->newPairCode();
    s->localIp = findLocalIp(s->preferredIp);
    s->ipCheckedAt = std::chrono::steady_clock::now();
    s->thread = std::thread([s] { s->run(); });
    return s;
}

void remote_stop(RemoteServer* s) {
    if (!s) return;
    s->quit = true;
    if (s->thread.joinable()) s->thread.join();
    for (auto& c : s->clients) closeSock(c->fd);
    closeSock(s->listener);
    delete s;
}

const char* remote_last_error(void) { return gLastError.c_str(); }

int remote_port(const RemoteServer* s) { return s ? s->port : 0; }

const char* remote_local_ip(RemoteServer* s) {
    std::lock_guard<std::mutex> lock(s->m);
    // The Wi-Fi network can change while the app runs (or the computer joins a hotspot).
    const auto now = std::chrono::steady_clock::now();
    if (now - s->ipCheckedAt > std::chrono::seconds(5)) {
        s->ipCheckedAt = now;
        s->localIp = findLocalIp(s->preferredIp);
    }
    s->ipOut = s->localIp;
    return s->ipOut.c_str();
}

const char* remote_pair_url(RemoteServer* s) {
    const std::string ip = remote_local_ip(s);
    std::lock_guard<std::mutex> lock(s->m);
    s->urlOut = ip.empty() ? "" : "http://" + ip + ":" + std::to_string(s->port) + "/?pair=" + s->pairCode;
    return s->urlOut.c_str();
}

const char* remote_addresses(RemoteServer* s) {
    std::string out;
    for (auto& e : listLocalIps()) out += e.first + "\t" + e.second + "\n";
    std::lock_guard<std::mutex> lock(s->m);
    s->addressesOut = out;
    return s->addressesOut.c_str();
}

void remote_set_address(RemoteServer* s, const char* ip) {
    std::lock_guard<std::mutex> lock(s->m);
    s->preferredIp = ip ? ip : "";
    s->localIp = findLocalIp(s->preferredIp);
    s->ipCheckedAt = std::chrono::steady_clock::now();
}

const char* remote_tokens(RemoteServer* s) {
    std::lock_guard<std::mutex> lock(s->m);
    s->tokensOut.clear();
    for (auto& t : s->tokens) s->tokensOut += t + "\n";
    return s->tokensOut.c_str();
}

void remote_set_tokens(RemoteServer* s, const char* lines) {
    std::lock_guard<std::mutex> lock(s->m);
    s->tokens.clear();
    std::string all = lines ? lines : "", t;
    for (char c : all + "\n") {
        if (c == '\n' || c == ',' || c == ' ') {
            if (t.size() == 32 && !s->tokenOk(t)) s->tokens.push_back(t);
            t.clear();
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) t += c;
    }
    while (s->tokens.size() > 16) s->tokens.erase(s->tokens.begin());
}

const char* remote_pair_code(RemoteServer* s) {
    std::lock_guard<std::mutex> lock(s->m);
    s->codeOut = s->pairCode;
    return s->codeOut.c_str();
}

int remote_next_event(RemoteServer* s, RemoteEvent* ev) {
    std::lock_guard<std::mutex> lock(s->m);
    if (s->events.empty()) return 0;
    s->current = std::move(s->events.front());
    s->events.pop_front();
    ev->type = s->current.type;
    ev->client = s->current.client;
    ev->data = s->current.data.data();
    ev->size = s->current.data.size();
    ev->name = s->current.name.c_str();
    return 1;
}

void remote_send(RemoteServer* s, int client, const char* text, size_t size) {
    if (!s) return;
    std::lock_guard<std::mutex> lock(s->m);
    const std::string msg(text, size);
    for (auto& c : s->clients)
        if (c->ws && !c->closing && (client < 0 || c->id == client)) s->wsSend(*c, msg);
}

const char* remote_clients(RemoteServer* s) {
    std::lock_guard<std::mutex> lock(s->m);
    s->clientsOut.clear();
    for (auto& c : s->clients)
        if (c->ws && !c->closing) s->clientsOut += c->label + "\n";
    return s->clientsOut.c_str();
}

int remote_client_count(RemoteServer* s) {
    std::lock_guard<std::mutex> lock(s->m);
    int n = 0;
    for (auto& c : s->clients) if (c->ws && !c->closing) ++n;
    return n;
}

void remote_disconnect_all(RemoteServer* s) {
    std::lock_guard<std::mutex> lock(s->m);
    s->tokens.clear();
    s->newPairCode();
    s->dropAll = true;
}

int remote_qr(const char* text, unsigned char* out, int cap) {
    std::vector<uint8_t> qr(qrcodegen_BUFFER_LEN_MAX), tmp(qrcodegen_BUFFER_LEN_MAX);
    if (!qrcodegen_encodeText(text, tmp.data(), qr.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                              qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        return 0;
    const int n = qrcodegen_getSize(qr.data());
    if (n * n > cap) return 0;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) out[y * n + x] = qrcodegen_getModule(qr.data(), x, y) ? 1 : 0;
    return n;
}

const char* remote_schema_json(void) { return kRemoteSchema; }

} // extern "C"
