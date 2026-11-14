#!/usr/bin/env bash
# Builds a universal (Apple Silicon + Intel) Dropocalypse.app, a .zip and a .dmg in ./dist.
#
#   ./scripts/build-app.sh            # version from the latest git tag, or 0.0.0-dev
#   VERSION=1.2.3 ./scripts/build-app.sh
set -euo pipefail

cd "$(dirname "$0")/.."
APP_NAME="Dropocalypse"
EXEC_NAME="Dropocalypse"
BUNDLE_ID="io.github.allenrobberson.dropocalypse"
VERSION="${VERSION:-$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || true)}"
VERSION="${VERSION:-0.0.0-dev}"
BUILD_NUMBER="${BUILD_NUMBER:-$(git rev-list --count HEAD 2>/dev/null || echo 1)}"

DIST="dist"
APP="$DIST/$APP_NAME.app"
rm -rf "$DIST"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

echo "==> Compiling ($VERSION, build $BUILD_NUMBER)"
swift build -c release --arch arm64 --arch x86_64
BIN_DIR="$(swift build -c release --arch arm64 --arch x86_64 --show-bin-path)"
cp "$BIN_DIR/$EXEC_NAME" "$APP/Contents/MacOS/$EXEC_NAME"

echo "==> Writing Info.plist"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$APP_NAME</string>
    <key>CFBundleDisplayName</key><string>$APP_NAME</string>
    <key>CFBundleExecutable</key><string>$EXEC_NAME</string>
    <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleVersion</key><string>$BUILD_NUMBER</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.music</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
    <key>NSAudioCaptureUsageDescription</key>
    <string>Dropocalypse listens to the audio your Mac is playing (for example Spotify) so the visuals can react to it.</string>
    <key>NSMicrophoneUsageDescription</key>
    <string>Dropocalypse listens to your microphone or line-in so the visuals and your logo can move with the music. Audio is analyzed live and never recorded or sent anywhere.</string>
</dict>
</plist>
PLIST

if [ -f "Resources/AppIcon.icns" ]; then
  cp "Resources/AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"
fi

echo "==> Signing (ad-hoc)"
codesign --force --deep --sign - "$APP"
codesign --verify --verbose "$APP"

echo "==> Packaging"
ZIP="$DIST/Dropocalypse-$VERSION-macOS.zip"
DMG="$DIST/Dropocalypse-$VERSION-macOS.dmg"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"

STAGING="$(mktemp -d)"
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"
if [ -f "docs/Dropocalypse-User-Guide.pdf" ]; then
  cp "docs/Dropocalypse-User-Guide.pdf" "$STAGING/Dropocalypse User Guide.pdf"
fi
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGING" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGING"

echo "==> Done"
ls -lh "$DIST"
