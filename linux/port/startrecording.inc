static void startRecording() {
    if (g.recorder.isRecording()) return;
    std::string dir = narrow(recordFolder());
    makeDirs(dir);
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char name[64];
    std::strftime(name, sizeof(name), "Dropocalypse %Y-%m-%d at %H.%M.%S.mp4", &tm);
    std::wstring path = widen(dir + "/" + name);
    std::wstring err;
    if (!g.recorder.start(path, g.settings.recordQuality, g.settings.r.portrait, err)) {
        showRecordNotice(L"Couldn't start recording: " + err);
        return;
    }
    g.audio.setRecordSink([](const float* stereo, uint32_t frames, uint32_t rate) {
        g.recorder.pushAudio(stereo, frames, rate);
    });
}

