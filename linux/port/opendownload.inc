static void openDownloadPage() {
    std::string url = g.settings.latestUrl.rfind("https://github.com/", 0) == 0 ? g.settings.latestUrl : kReleasesPage;
    openWithDesktop(url);
}

