/* Phone remote: a small web server on the local network that serves the phone page and talks to
 * it over a WebSocket. Shared by the Mac (through Swift Package Manager), Windows and Linux apps,
 * so every platform speaks exactly the same protocol and serves exactly the same page.
 *
 * Threading: the server runs its own thread. Every function here may be called from the app's
 * main thread; events are queued and read with remote_next_event().
 *
 * Protocol (JSON text messages; see shared/remote/schema.json for the option ids):
 *   phone -> GET /                     the phone page (shared/remote/page.html)
 *   phone -> GET /ws?pair=CODE         first connection, with the one-time code from the QR code
 *   phone -> GET /ws?token=TOKEN       reconnecting later with the token the server handed out
 *   server -> {"t":"hello","token":..,"platform":..,"schema":{..}}   sent by this library on connect
 *   app    -> {"t":"state","v":{id:value,..}}   full state (on connect) or the values that changed
 *   app    -> {"t":"styles",..}                 the style list (when it changes)
 *   phone  -> {"t":"set","id":..,"v":..}        change an option
 *   phone  -> {"t":"act","id":..,"i":..,"v":..} press a button (i / v are optional arguments)
 *   phone  -> POST /upload?token=TOKEN&name=FILE   raw image bytes (a new logo)
 */
#ifndef DROPOCALYPSE_REMOTE_SERVER_H
#define DROPOCALYPSE_REMOTE_SERVER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RemoteServer RemoteServer;

enum {
    REMOTE_EVENT_CONNECTED = 1,    /* a phone paired or reconnected: send it the full state */
    REMOTE_EVENT_DISCONNECTED = 2, /* a phone went away */
    REMOTE_EVENT_MESSAGE = 3,      /* data/size: one JSON text message from the phone */
    REMOTE_EVENT_UPLOAD = 4        /* data/size: the file's bytes; name: its file name */
};

typedef struct RemoteEvent {
    int type;
    int client;       /* which phone (ids are never reused while the server runs) */
    const char* data; /* valid until the next remote_next_event() call */
    size_t size;
    const char* name; /* REMOTE_EVENT_UPLOAD only */
} RemoteEvent;

/* Starts listening on preferredPort (or the next free port after it). platform is "mac",
 * "windows" or "linux"; the page uses it to lay the options out like that app's controls
 * window. Returns NULL on failure (see remote_last_error()). */
RemoteServer* remote_start(int preferredPort, const char* platform);
void remote_stop(RemoteServer* s);
const char* remote_last_error(void);

int remote_port(const RemoteServer* s);
/* "http://192.168.1.20:47800/?pair=K7PQ2MXA", or "" when no local network address was found.
 * The pairing code changes after every successful pairing. The string stays valid until the
 * next call. */
const char* remote_pair_url(RemoteServer* s);
/* Just the pairing code ("K7PQ2MXA"). Valid until the next call. */
const char* remote_pair_code(RemoteServer* s);
/* The computer's address on the local network ("" when there is none). */
const char* remote_local_ip(RemoteServer* s);

/* This computer's local network addresses, one per line: "192.168.1.20<TAB>en0" (the name may be
 * empty). Valid until the next call. */
const char* remote_addresses(RemoteServer* s);
/* Use this address in the pairing URL (when the computer has it); "" = choose automatically. */
void remote_set_address(RemoteServer* s, const char* ip);

/* Paired phones' tokens, one per line, so the app can save them and phones reconnect after a
 * restart without scanning again. Changes when a phone pairs or on Disconnect. */
const char* remote_tokens(RemoteServer* s);
void remote_set_tokens(RemoteServer* s, const char* lines);

/* Returns 1 and fills *ev when an event is waiting, 0 when there are none. */
int remote_next_event(RemoteServer* s, RemoteEvent* ev);
/* Sends a text message to one phone, or to every paired phone when client < 0. */
void remote_send(RemoteServer* s, int client, const char* text, size_t size);

/* Connected phones, one per line ("iPhone (192.168.1.31)"). Valid until the next call. */
const char* remote_clients(RemoteServer* s);
int remote_client_count(RemoteServer* s);
/* Disconnects every phone, forgets their tokens and makes a new pairing code. */
void remote_disconnect_all(RemoteServer* s);

/* QR code for text: writes size*size modules (1 = dark) into out and returns size,
 * or 0 if it doesn't fit in cap bytes. */
int remote_qr(const char* text, unsigned char* out, int cap);

/* The shared option schema (shared/remote/schema.json), for the apps' own checks. */
const char* remote_schema_json(void);

#ifdef __cplusplus
}
#endif

#endif
