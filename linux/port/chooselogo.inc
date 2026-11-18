static void chooseLogo() {
    if (g.pendingReplace < 0 && (int)g.settings.items.size() >= kMaxOverlayItems) {
        g.errorMessage = L"That's the most: 4 logos or text items at once. Remove one to add another.";
        return;
    }
    const char* title = g.pendingReplace >= 0 ? "Choose a logo to replace this one"
                                              : "Add a logo (PNG with a transparent background works best)";
    runDialog(1,
              {"zenity", "--file-selection", std::string("--title=") + title,
               "--file-filter=Images | *.png *.PNG *.jpg *.JPG *.jpeg *.JPEG *.gif *.GIF *.bmp *.BMP *.tga *.TGA",
               "--file-filter=All files | *"},
              {"kdialog", "--title", title, "--getopenfilename", homeDir(),
               "Images (*.png *.jpg *.jpeg *.gif *.bmp *.tga)"});
}
