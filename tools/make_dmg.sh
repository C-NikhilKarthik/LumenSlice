#!/usr/bin/env bash
# Build a distributable .dmg for LumenSlice.
#
# Wraps tools/make_app.sh (which produces the self-contained, ad-hoc-signed
# dist/LumenSlice.app) into a drag-to-Applications disk image. No third-party
# tools required -- uses only hdiutil, so it runs unchanged on CI.
#
# Output: dist/LumenSlice.dmg
#
# Note: builds for the host architecture (GitHub's macos-14 runner is arm64).
# The .app inside is ad-hoc signed, NOT notarized, so on first launch the
# recipient must right-click -> Open (see README). Notarization needs a paid
# Apple Developer ID; the release workflow has commented hooks for it.
set -euo pipefail
cd "$(dirname "$0")/.."

APP_NAME="LumenSlice"
VOL_NAME="$APP_NAME"
DMG="dist/$APP_NAME.dmg"

# Build + assemble the .app (honours VERSION from the environment).
tools/make_app.sh

echo "==> Staging disk image contents..."
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT
cp -R "dist/$APP_NAME.app" "$STAGING/"
ln -s /Applications "$STAGING/Applications"   # drag-to-install target

echo "==> Creating ${DMG}..."
rm -f "$DMG"
hdiutil create \
    -volname "$VOL_NAME" \
    -srcfolder "$STAGING" \
    -fs HFS+ \
    -format UDZO \
    -ov \
    "$DMG" >/dev/null

# Ad-hoc sign the image too (harmless locally; keeps the chain consistent).
codesign --force --sign - "$DMG" 2>/dev/null || true

echo ""
echo "Done:"
echo "  $DMG   ($(du -h "$DMG" | cut -f1))   <-- distribute this"
echo ""
echo "Recipient: open the .dmg, drag LumenSlice.app to Applications, then"
echo "right-click -> Open on first launch (unsigned/un-notarized). Or run:"
echo "  xattr -dr com.apple.quarantine /Applications/LumenSlice.app"
