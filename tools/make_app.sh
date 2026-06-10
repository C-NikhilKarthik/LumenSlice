#!/usr/bin/env bash
# Package LumenSlice into a self-contained, shareable macOS .app bundle.
#
# DCMTK is linked statically (see Package.swift), so the only thing we need to
# carry alongside the binary is DCMTK's data dictionary (required to parse
# Implicit-VR DICOM). The result is ad-hoc code-signed and zipped, ready to send.
#
# Output: dist/LumenSlice.app  and  dist/LumenSlice.zip
#
# Note: builds for the host architecture (Apple Silicon -> arm64). The recipient
# needs the same arch. For a notarized, Gatekeeper-clean app you need an Apple
# Developer ID certificate; see the README.
set -euo pipefail
cd "$(dirname "$0")/.."

APP_NAME="LumenSlice"
BUNDLE_ID="com.flamapp.lumenslice"
VERSION="0.1.0"
DICT_DIR="$(brew --prefix dcmtk)/share/dcmtk-3.7.0"

echo "==> Building release..."
swift build -c release --product "$APP_NAME"
BIN="$(swift build -c release --show-bin-path)/$APP_NAME"

APP="dist/$APP_NAME.app"
echo "==> Assembling ${APP}..."
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/$APP_NAME"

# Bundle the DICOM data dictionary the app points DCMDICTPATH at.
for d in dicom.dic private.dic acrnema.dic; do
    [ -f "$DICT_DIR/$d" ] && cp "$DICT_DIR/$d" "$APP/Contents/Resources/"
done

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>$APP_NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
  <key>CFBundleName</key><string>$APP_NAME</string>
  <key>CFBundleDisplayName</key><string>$APP_NAME</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.medical</string>
</dict></plist>
PLIST

echo "==> Ad-hoc code-signing (required for arm64 to run at all)..."
codesign --force --deep --sign - "$APP"
codesign --verify --verbose "$APP" 2>&1 | sed 's/^/    /'

echo "==> Zipping..."
ditto -c -k --keepParent "$APP" "dist/$APP_NAME.zip"

echo ""
echo "Done:"
echo "  $APP"
echo "  dist/$APP_NAME.zip   ($(du -h "dist/$APP_NAME.zip" | cut -f1))   <-- send this"
echo ""
echo "Recipient: unzip, drag LumenSlice.app to /Applications, then right-click -> Open"
echo "(first launch only, to bypass Gatekeeper). Or run:"
echo "  xattr -dr com.apple.quarantine /Applications/LumenSlice.app"
