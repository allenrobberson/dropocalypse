// Tests for the phone remote server: pairing, one-time codes, tokens, WebSocket messages,
// uploads, Disconnect, the QR encoder and the schema. Talks to the server over 127.0.0.1.
#include "remote_server.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
static void closeSock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
static void closeSock(sock_t s) { close(s); }
#endif

static int failures = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static sock_t dial(int port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
#ifdef _WIN32
    DWORD tv = 2000;
#else
    timeval tv{2, 0};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    connect(s, (sockaddr*)&a, sizeof(a));
    return s;
}

static void sendAll(sock_t s, const std::string& d) { send(s, d.data(), (int)d.size(), 0); }

static std::string readUntil(sock_t s, const char* marker, size_t atLeast = 0) {
    std::string got;
    char buf[8192];
    for (int i = 0; i < 400; ++i) {
        if (got.find(marker) != std::string::npos && got.size() >= atLeast) break;
        int n = (int)recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        got.append(buf, (size_t)n);
    }
    return got;
}

static std::string upgrade(const std::string& query) {
    return "GET /ws?" + query + " HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
           "User-Agent: Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X)\r\n\r\n";
}

static std::string maskedText(const std::string& payload) {
    std::string f;
    f += (char)0x81;
    f += (char)(0x80 | payload.size());
    const unsigned char mask[4] = {1, 2, 3, 4};
    for (unsigned char m : mask) f += (char)m;
    for (size_t i = 0; i < payload.size(); ++i) f += (char)(payload[i] ^ mask[i & 3]);
    return f;
}

static bool waitEvent(RemoteServer* s, RemoteEvent& ev, int type) {
    for (int i = 0; i < 200; ++i) {
        while (remote_next_event(s, &ev))
            if (ev.type == type) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static std::string between(const std::string& s, const std::string& a, const std::string& b) {
    size_t p = s.find(a);
    if (p == std::string::npos) return "";
    p += a.size();
    return s.substr(p, s.find(b, p) - p);
}

int main() {
#ifdef _WIN32
    WSADATA d;
    WSAStartup(MAKEWORD(2, 2), &d);
#endif
    RemoteServer* s = remote_start(47900, "linux");
    CHECK(s != nullptr);
    if (!s) return 1;
    const int port = remote_port(s);
    CHECK(port >= 47900);

    // The pairing URL carries an 8-character code (when this machine has a network address).
    std::string url = remote_pair_url(s);
    std::string code = remote_pair_code(s);
    CHECK(code.size() == 8);
    if (!url.empty()) CHECK(url.find("/?pair=" + code) != std::string::npos);

    // The page itself is served to anyone on the network (it holds no secrets).
    {
        sock_t c = dial(port);
        sendAll(c, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
        std::string r = readUntil(c, "</html>");
        CHECK(r.find("200 OK") != std::string::npos);
        CHECK(r.find("Dropocalypse remote") != std::string::npos);
        closeSock(c);
    }
    // A wrong code is refused.
    {
        sock_t c = dial(port);
        sendAll(c, upgrade("pair=WRONG123"));
        std::string r = readUntil(c, "\r\n\r\n");
        CHECK(r.find("403") != std::string::npos);
        closeSock(c);
    }
    // A web page from another site can't connect, even with the code.
    if (!code.empty()) {
        sock_t c = dial(port);
        std::string req = upgrade("pair=" + code);
        req.insert(req.size() - 2, "Origin: http://evil.example\r\n");
        sendAll(c, req);
        std::string r = readUntil(c, "\r\n\r\n");
        CHECK(r.find("403") != std::string::npos);
        closeSock(c);
    }

    std::string token;
    if (!code.empty()) {
        sock_t c = dial(port);
        sendAll(c, upgrade("pair=" + code));
        std::string r = readUntil(c, "\"schema\"", 0);
        r += readUntil(c, "}}}");
        CHECK(r.find("101 Switching Protocols") != std::string::npos);
        CHECK(r.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);   // RFC 6455 example key
        CHECK(r.find("\"t\":\"hello\"") != std::string::npos);
        CHECK(r.find("\"platform\":\"linux\"") != std::string::npos);
        token = between(r, "\"token\":\"", "\"");
        CHECK(token.size() == 32);
        RemoteEvent ev;
        CHECK(waitEvent(s, ev, REMOTE_EVENT_CONNECTED));
        const int id = ev.client;
        CHECK(remote_client_count(s) == 1);
        CHECK(std::string(remote_clients(s)).find("iPhone (127.0.0.1)") != std::string::npos);

        // Phone -> app.
        sendAll(c, maskedText("{\"t\":\"set\",\"id\":\"motion.pulse\",\"v\":0.5}"));
        CHECK(waitEvent(s, ev, REMOTE_EVENT_MESSAGE));
        CHECK(std::string(ev.data, ev.size) == "{\"t\":\"set\",\"id\":\"motion.pulse\",\"v\":0.5}");
        CHECK(ev.client == id);
        // App -> phone.
        const char* msg = "{\"t\":\"state\",\"v\":{\"motion.pulse\":0.5}}";
        remote_send(s, -1, msg, std::strlen(msg));
        std::string got = readUntil(c, "motion.pulse");
        CHECK(got.size() >= 2 && (unsigned char)got[0] == 0x81 && got.find(msg) == 2);

        // The code was one-time: it changed, and the old one no longer works.
        CHECK(std::string(remote_pair_code(s)) != code);
        sock_t c2 = dial(port);
        sendAll(c2, upgrade("pair=" + code));
        CHECK(readUntil(c2, "\r\n\r\n").find("403") != std::string::npos);
        closeSock(c2);

        // The token reconnects.
        sock_t c3 = dial(port);
        sendAll(c3, upgrade("token=" + token));
        CHECK(readUntil(c3, "\"hello\"").find("101") != std::string::npos);
        CHECK(waitEvent(s, ev, REMOTE_EVENT_CONNECTED));

        // Paired phones survive a restart: the app saves the tokens and gives them back.
        CHECK(std::string(remote_tokens(s)) == token + "\n");
        {
            RemoteServer* s2 = remote_start(48100, "linux");
            CHECK(s2 != nullptr);
            remote_set_tokens(s2, (token + "\n").c_str());
            sock_t r1 = dial(remote_port(s2));
            sendAll(r1, upgrade("token=" + token));
            CHECK(readUntil(r1, "\"hello\"").find("101") != std::string::npos);
            closeSock(r1);
            remote_stop(s2);
        }

        // Upload with the token.
        sock_t c4 = dial(port);
        sendAll(c4, "POST /upload?token=" + token + "&name=logo%20one.png HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nabcde");
        CHECK(readUntil(c4, "ok").find("200 OK") != std::string::npos);
        CHECK(waitEvent(s, ev, REMOTE_EVENT_UPLOAD));
        CHECK(std::string(ev.data, ev.size) == "abcde");
        CHECK(std::string(ev.name) == "logo one.png");
        closeSock(c4);
        // And without it.
        sock_t c5 = dial(port);
        sendAll(c5, "POST /upload?token=nope HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nabcde");
        CHECK(readUntil(c5, "\r\n\r\n").find("403") != std::string::npos);
        closeSock(c5);

        // Disconnect: phones are dropped and their tokens stop working.
        remote_disconnect_all(s);
        CHECK(waitEvent(s, ev, REMOTE_EVENT_DISCONNECTED));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(remote_client_count(s) == 0);
        CHECK(std::string(remote_tokens(s)).empty());
        sock_t c6 = dial(port);
        sendAll(c6, upgrade("token=" + token));
        CHECK(readUntil(c6, "\r\n\r\n").find("403") != std::string::npos);
        closeSock(c6);
        closeSock(c);
        closeSock(c3);
    }

    // QR code: the URL encodes into a square of modules.
    unsigned char qr[177 * 177];
    int n = remote_qr("http://192.168.1.20:47800/?pair=K7PQ2MXA", qr, sizeof(qr));
    CHECK(n >= 21 && (n - 17) % 4 == 0);
    CHECK(qr[0] == 1 && qr[n - 1] == 1);   // finder patterns in the corners

    remote_set_address(s, "10.9.8.7");   // not this computer's: falls back to an automatic choice
    CHECK(std::string(remote_local_ip(s)) != "10.9.8.7");
    remote_addresses(s);

    CHECK(std::strstr(remote_schema_json(), "\"sections\"") != nullptr);

    remote_stop(s);
    if (failures) {
        std::printf("%d remote server test(s) failed\n", failures);
        return 1;
    }
    std::printf("remote server tests passed\n");
    return 0;
}
