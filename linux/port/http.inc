static bool httpsGetUrl(const std::string& url, std::string& out, std::string& error, size_t maxBytes,
                        const std::function<void(uint64_t, uint64_t)>& progress = nullptr,
                        const char* extraHeader = nullptr) {
    if (url.rfind("https://", 0) != 0) { error = "Bad download address."; return false; }
    CURL* c = curl_easy_init();
    if (!c) { error = "Couldn't start the network request."; return false; }
    struct Sink {
        std::string* out;
        size_t max;
        bool tooBig = false;
        const std::function<void(uint64_t, uint64_t)>* progress;
    } sink{&out, maxBytes, false, &progress};
    auto write = +[](char* p, size_t sz, size_t n, void* user) -> size_t {
        auto* s = static_cast<Sink*>(user);
        s->out->append(p, sz * n);
        if (s->out->size() > s->max) { s->tooBig = true; return 0; }
        return sz * n;
    };
    auto xfer = +[](void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) -> int {
        auto* s = static_cast<Sink*>(user);
        if (*s->progress) (*s->progress)((uint64_t)now, (uint64_t)total);
        return 0;
    };
    struct curl_slist* headers = nullptr;
    if (extraHeader) headers = curl_slist_append(headers, extraHeader);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Dropocalypse-updater");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 100L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    if (headers) curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    if (sink.tooBig) { error = "Download was larger than expected."; return false; }
    if (rc != CURLE_OK) { error = "Couldn't reach GitHub. Check your internet connection."; return false; }
    if (status != 200) { error = "GitHub didn't answer (" + std::to_string(status) + "). Try again later."; return false; }
    return true;
}

static bool httpsGet(const wchar_t* host, const wchar_t* path, std::string& out, std::string& error) {
    return httpsGetUrl("https://" + narrow(host) + narrow(path), out, error, 2'000'000, nullptr,
                       "Accept: application/vnd.github+json");
}

