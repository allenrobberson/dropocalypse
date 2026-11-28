#!/usr/bin/env bash
# Builds a universal (Apple Silicon + Intel) Dropocalypse.app, a .zip and a .dmg in ./dist.
#
#   ./scripts/build-app.sh            # version from the latest git tag, or 0.0.0-dev
#   VERSION=1.2.3 ./scripts/build-app.sh
#
# Signing (both optional; without them the app is ad-hoc signed):
#   SIGN_IDENTITY="Developer ID Application: ..."   sign with hardened runtime for the GitHub download
#   NOTARY_KEY=path/to/AuthKey.p8 NOTARY_KEY_ID=... NOTARY_ISSUER=...   also notarize and staple
#
# Mac App Store edition (sandboxed, no self-updater) -> dist-appstore/Dropocalypse-<version>.pkg:
#   EDITION=appstore SIGN_IDENTITY="Apple Distribution: ..." INSTALLER_IDENTITY="3rd Party Mac Developer Installer: ..." \
#     PROVISION_PROFILE=path/to/profile.provisionprofile ./scripts/build-app.sh
set -euo pipefail

cd "$(dirname "$0")/.."
APP_NAME="Dropocalypse"
EXEC_NAME="Dropocalypse"
BUNDLE_ID="io.github.allenrobberson.dropocalypse"
VERSION="${VERSION:-$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || true)}"
VERSION="${VERSION:-0.0.0-dev}"
BUILD_NUMBER="${BUILD_NUMBER:-$(git rev-list --count HEAD 2>/dev/null || echo 1)}"

EDITION="${EDITION:-github}"
SIGN_IDENTITY="${SIGN_IDENTITY:-}"
DIST="dist"
SWIFT_FLAGS=()
if [ "$EDITION" = "appstore" ]; then
  DIST="dist-appstore"
  SWIFT_FLAGS=(-Xswiftc -DAPP_STORE)
fi
APP="$DIST/$APP_NAME.app"
rm -rf "$DIST"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

echo "==> Compiling ($VERSION, build $BUILD_NUMBER)"
swift build -c release --arch arm64 --arch x86_64 ${SWIFT_FLAGS[@]+"${SWIFT_FLAGS[@]}"}
BIN_DIR="$(swift build -c release --arch arm64 --arch x86_64 ${SWIFT_FLAGS[@]+"${SWIFT_FLAGS[@]}"} --show-bin-path)"
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
    <key>ITSAppUsesNonExemptEncryption</key><false/>
    <key>NSAudioCaptureUsageDescription</key>
    <string>Dropocalypse listens to the audio your Mac is playing (for example Spotify) so the visuals can react to it.</string>
    <key>NSLocalNetworkUsageDescription</key>
    <string>Dropocalypse uses your local network for the phone remote, so you can control the visuals from your phone's web browser. It is off until you turn it on.</string>
    <key>NSMicrophoneUsageDescription</key>
    <string>Dropocalypse listens to your microphone or line-in so the visuals and your logo can move with the music. Audio is analyzed live and never recorded or sent anywhere.</string>
</dict>
</plist>
PLIST

if [ -f "Resources/AppIcon.icns" ]; then
  cp "Resources/AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"
fi

# MilkDrop presets: the bundled presets and textures, and the projectM engine (LGPL, built by
# scripts/build-projectm.sh). Without the engine the app still runs; the MilkDrop styles just
# don't appear.
mkdir -p "$APP/Contents/Resources/MilkDrop"
cp -R milkdrop/presets milkdrop/textures milkdrop/CREDITS.md "$APP/Contents/Resources/MilkDrop/"
PROJECTM_LIB="${PROJECTM_LIB:-build/projectm/lib/libprojectM-4.dylib}"
if [ -f "$PROJECTM_LIB" ]; then
  mkdir -p "$APP/Contents/Frameworks"
  cp -L "$PROJECTM_LIB" "$APP/Contents/Frameworks/libprojectM-4.dylib"
  echo "==> Bundled the MilkDrop engine ($(lipo -archs "$APP/Contents/Frameworks/libprojectM-4.dylib"))"
else
  echo "==> No MilkDrop engine at $PROJECTM_LIB (run scripts/build-projectm.sh); building without it"
fi
sign_engine() {
  # Nested code is signed first, with the same identity, so the hardened runtime loads it.
  if [ -f "$APP/Contents/Frameworks/libprojectM-4.dylib" ]; then
    codesign --force --options runtime --timestamp --sign "$1" "$APP/Contents/Frameworks/libprojectM-4.dylib"
  fi
}

ENT_DIR="scripts/entitlements"

if [ "$EDITION" = "appstore" ]; then
  : "${SIGN_IDENTITY:?set SIGN_IDENTITY to the Apple Distribution identity}"
  : "${INSTALLER_IDENTITY:?set INSTALLER_IDENTITY to the Mac Installer Distribution identity}"
  : "${PROVISION_PROFILE:?set PROVISION_PROFILE to the Mac App Store provisioning profile}"
  echo "==> Signing for the Mac App Store"
  cp "$PROVISION_PROFILE" "$APP/Contents/embedded.provisionprofile"
  sign_engine "$SIGN_IDENTITY"
  codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" \
    --entitlements "$ENT_DIR/AppStore.entitlements" "$APP"
  codesign --verify --strict --verbose=2 "$APP"
  codesign -d --entitlements - "$APP" 2>/dev/null | head -40 || true
  PKG="$DIST/Dropocalypse-$VERSION.pkg"
  productbuild --component "$APP" /Applications --sign "$INSTALLER_IDENTITY" "$PKG"
  pkgutil --check-signature "$PKG"
  echo "==> Done"
  ls -lh "$DIST"
  exit 0
fi

if [ -n "$SIGN_IDENTITY" ]; then
  echo "==> Signing with $SIGN_IDENTITY (hardened runtime)"
  sign_engine "$SIGN_IDENTITY"
  codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" \
    --entitlements "$ENT_DIR/DeveloperID.entitlements" "$APP"
else
  echo "==> Signing (ad-hoc)"
  codesign --force --deep --sign - "$APP"
fi
codesign --verify --strict --verbose=2 "$APP"

ZIP="$DIST/Dropocalypse-$VERSION-macOS.zip"
DMG="$DIST/Dropocalypse-$VERSION-macOS.dmg"

echo "==> Building disk image"
STAGING="$(mktemp -d)"
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"
if [ -f "docs/Dropocalypse-User-Guide.pdf" ]; then
  cp "docs/Dropocalypse-User-Guide.pdf" "$STAGING/Dropocalypse User Guide.pdf"
fi
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGING" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGING"

if [ -n "$SIGN_IDENTITY" ]; then
  codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG"
  if [ -n "${NOTARY_KEY:-}" ]; then
    echo "==> Notarizing (this usually takes a few minutes)"
    # If Apple is slow, don't block the release: once Apple accepts the submission, macOS
    # finds the notarization online even without a stapled ticket.
    if xcrun notarytool submit "$DMG" --key "$NOTARY_KEY" --key-id "$NOTARY_KEY_ID" \
         --issuer "$NOTARY_ISSUER" --wait --timeout "${NOTARY_TIMEOUT:-20m}" | tee "$DIST/notary.log" \
       && grep -q "status: Accepted" "$DIST/notary.log"; then
      # Apple's ticket servers can take a moment to publish a fresh approval (stapler error 68),
      # so retry; if it still isn't there, ship anyway: macOS finds the approval online.
      staple() {
        for i in 1 2 3 4 5 6; do
          xcrun stapler staple "$1" && return 0
          echo "Ticket not available yet; retrying in 20 s ($i/6)"
          sleep 20
        done
        echo "::warning::Couldn't staple $1; it is notarized and macOS will verify it online."
        return 0
      }
      staple "$DMG"
      staple "$APP"     # the app inside was notarized with the disk image
      spctl --assess --type execute --verbose "$APP" || true
    else
      echo "::error::Notarization was not accepted. Refusing to publish a Mac build that is not notarized."
      cat "$DIST/notary.log" || true
      exit 1
    fi
    rm -f "$DIST/notary.log"
  fi
fi

echo "==> Zipping"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"

echo "==> Done"
ls -lh "$DIST"
